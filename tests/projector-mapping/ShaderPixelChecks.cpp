// Pixel checks bake the actual production shaders and render them through QRhi.
// In particular, neutral calibration must preserve negative extended-linear HDR
// components, while a mesh must cover only its configured output geometry.
#include <QGuiApplication>
#include <QFile>
#include <QImage>
#include <QMatrix4x4>
#include <QtCore/qfloat16.h>
#include <private/qrhi_p.h>
#if defined(__APPLE__)
#include <private/qrhimetal_p.h>
#elif defined(_WIN32)
#include <private/qrhid3d11_p.h>
#endif
#if __has_include(<QtShaderTools/rhi/qshaderbaker.h>)
#include <QtShaderTools/rhi/qshaderbaker.h>
#else
#include <QtShaderTools/private/qshaderbaker_p.h>
#endif
#include <memory>
#include <iostream>
#include <cstring>
#include <cmath>
#include <vector>
#include <stdexcept>

QByteArray productionLiteral(const char* path, const char* marker)
{
  QFile file(QString::fromUtf8(PROJECTOR_GFX_ROOT) + QString::fromUtf8(path));
  if(!file.open(QFile::ReadOnly)) throw std::runtime_error("Production shader source missing");
  const auto source = file.readAll();
  const auto method = source.indexOf(marker);
  const auto begin = source.indexOf("R\"_(", method);
  const auto end = source.indexOf(")_\";", begin);
  if(method < 0 || begin < 0 || end < 0) throw std::runtime_error("Production shader literal not found");
  return source.mid(begin + 4, end - begin - 4);
}

QShader bake(const QByteArray& source, QShader::Stage stage)
{
  QShaderBaker baker;
  baker.setSourceString(source, stage);
#if defined(__APPLE__)
  baker.setGeneratedShaders({{QShader::MslShader, QShaderVersion(12)}});
#else
  baker.setGeneratedShaders({{QShader::HlslShader, QShaderVersion(50)}});
#endif
  baker.setGeneratedShaderVariants({QShader::StandardShader});
  baker.setPerTargetCompilation(true);
  auto result = baker.bake();
  if(!result.isValid()) throw std::runtime_error(baker.errorMessage().toStdString());
  return result;
}

void checkPixels(QRhi* rhi, const QShader& vs, const QShader& fs, bool hdr)
{
    const QSize size(128, 64);
    auto source = std::unique_ptr<QRhiTexture>(rhi->newTexture(hdr ? QRhiTexture::RGBA16F : QRhiTexture::RGBA8, size));
    auto output = std::unique_ptr<QRhiTexture>(rhi->newTexture(hdr ? QRhiTexture::RGBA16F : QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    if(!source->create() || !output->create()) throw std::runtime_error("Texture allocation");
    auto target = std::unique_ptr<QRhiTextureRenderTarget>(rhi->newTextureRenderTarget(QRhiTextureRenderTargetDescription(output.get())));
    auto pass = std::unique_ptr<QRhiRenderPassDescriptor>(target->newCompatibleRenderPassDescriptor());
    target->setRenderPassDescriptor(pass.get()); target->create();
    auto buffer = [&](int bytes, QRhiBuffer::UsageFlags usage) {
      auto b = std::unique_ptr<QRhiBuffer>(rhi->newBuffer(QRhiBuffer::Dynamic, usage, bytes));
      if(!b->create()) throw std::runtime_error("Buffer allocation");
      return b;
    };
    auto vertices = buffer(24 * sizeof(float), QRhiBuffer::VertexBuffer);
    auto renderer = buffer(80, QRhiBuffer::UniformBuffer);
    auto rect = buffer(16, QRhiBuffer::UniformBuffer);
    auto blend = buffer(32, QRhiBuffer::UniformBuffer);
    auto warp = buffer(64, QRhiBuffer::UniformBuffer);
    auto calibration = buffer(32, QRhiBuffer::UniformBuffer);
    auto sampler = std::unique_ptr<QRhiSampler>(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    sampler->create();
    auto bindings = std::unique_ptr<QRhiShaderResourceBindings>(rhi->newShaderResourceBindings());
    using B = QRhiShaderResourceBinding;
    bindings->setBindings({B::uniformBuffer(0, B::VertexStage, renderer.get()), B::uniformBuffer(2, B::FragmentStage, rect.get()), B::sampledTexture(3, B::FragmentStage, source.get(), sampler.get()), B::uniformBuffer(4, B::FragmentStage, blend.get()), B::uniformBuffer(5, B::FragmentStage, warp.get()), B::uniformBuffer(6, B::FragmentStage, calibration.get())});
    bindings->create();
    auto pipeline = std::unique_ptr<QRhiGraphicsPipeline>(rhi->newGraphicsPipeline());
    pipeline->setShaderStages({{QRhiShaderStage::Vertex, vs}, {QRhiShaderStage::Fragment, fs}});
    QRhiVertexInputLayout layout;
    layout.setBindings({{8}, {8}});
    layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}, {1, 1, QRhiVertexInputAttribute::Float2, 0}});
    pipeline->setVertexInputLayout(layout);
    pipeline->setShaderResourceBindings(bindings.get());
    pipeline->setRenderPassDescriptor(pass.get());
    pipeline->setCullMode(QRhiGraphicsPipeline::None);
    if(!pipeline->create()) throw std::runtime_error("Production shader pipeline creation");
    QImage white(size, QImage::Format_RGBA8888); white.fill(Qt::white);
    std::vector<qfloat16> colors(size.width() * size.height() * 4);
    for(int i = 0; i < size.width() * size.height(); ++i)
    { colors[i * 4] = qfloat16(-.25f); colors[i * 4 + 1] = qfloat16(.5f); colors[i * 4 + 2] = qfloat16(1.5f); colors[i * 4 + 3] = qfloat16(1.f); }
    // Two triangles: the projected mesh covers the left 75% of the output.
    const float geometry[] = {-1, 1, -1, -1, .5f, 1, .5f, 1, -1, -1, .5f, -1,
                               0, 1,  0, 0,  1, 1,  1, 1,  0, 0,  1, 0};
    float renderData[20]{};
    auto correction = rhi->clipSpaceCorrMatrix(); std::memcpy(renderData, correction.constData(), 64);
    renderData[16] = size.width(); renderData[17] = size.height();
    const float rectData[] = {0, 0, 1, 1};
    const float blendData[] = {0, 0, 0, 0, 1, 1, 1, 1};
    float warpData[16]{}; warpData[12] = hdr ? 0.f : -1.f;
    const float colorData[] = {hdr ? 1.f : .5f, 1, 1, 1, 0, 0, 0, 0};
    QRhiCommandBuffer* cb{};
    if(rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) throw std::runtime_error("Frame begin");
    auto* updates = rhi->nextResourceUpdateBatch();
    if(hdr)
    {
      QRhiTextureSubresourceUploadDescription upload{QByteArray(reinterpret_cast<const char*>(colors.data()), colors.size() * sizeof(qfloat16))};
      updates->uploadTexture(source.get(), QRhiTextureUploadDescription{{QRhiTextureUploadEntry{0, 0, upload}}});
    }
    else updates->uploadTexture(source.get(), white);
    updates->updateDynamicBuffer(vertices.get(), 0, sizeof(geometry), geometry);
    updates->updateDynamicBuffer(renderer.get(), 0, sizeof(renderData), renderData);
    updates->updateDynamicBuffer(rect.get(), 0, sizeof(rectData), rectData);
    updates->updateDynamicBuffer(blend.get(), 0, sizeof(blendData), blendData);
    updates->updateDynamicBuffer(warp.get(), 0, sizeof(warpData), warpData);
    updates->updateDynamicBuffer(calibration.get(), 0, sizeof(colorData), colorData);
    cb->beginPass(target.get(), Qt::black, {1.f, 0}, updates);
    cb->setGraphicsPipeline(pipeline.get()); cb->setShaderResources(bindings.get());
    cb->setViewport(QRhiViewport(0, 0, size.width(), size.height()));
    const QRhiCommandBuffer::VertexInput inputs[] = {{vertices.get(), 0}, {vertices.get(), 48}};
    cb->setVertexInput(0, 2, inputs); cb->draw(6); cb->endPass();
    QRhiReadbackResult result;
    auto* read = rhi->nextResourceUpdateBatch(); read->readBackTexture(QRhiReadbackDescription(output.get()), &result); cb->resourceUpdate(read);
    rhi->endOffscreenFrame(); rhi->finish();
    if(result.data.size() != 128 * 64 * 4 * (hdr ? int(sizeof(qfloat16)) : 1))
      throw std::runtime_error("Readback size mismatch");
    auto at = [&](int x, int channel) {
      const int index = (32 * 128 + x) * 4 + channel;
      if(hdr) return float(reinterpret_cast<const qfloat16*>(result.data.constData())[index]);
      return reinterpret_cast<const unsigned char*>(result.data.constData())[index] / 255.f;
    };
    const float expected[] = {hdr ? -.25f : .5f, hdr ? .5f : 1.f, hdr ? 1.5f : 1.f};
    for(int channel = 0; channel < 3; ++channel)
      if(std::abs(at(64, channel) - expected[channel]) > (hdr ? .0001f : .01f)
         || std::abs(at(120, channel)) > .01f)
        throw std::runtime_error(std::string(hdr ? "Neutral HDR" : "Mesh / RGB")
                                 + " pixel mismatch on channel " + std::to_string(channel)
                                 + ": " + std::to_string(at(64, channel)));
}

int main(int argc, char** argv)
{
  QGuiApplication app(argc, argv);
  try
  {
#if defined(__APPLE__)
    QRhiMetalInitParams params;
    std::unique_ptr<QRhi> rhi{QRhi::create(QRhi::Metal, &params)};
#elif defined(_WIN32)
    QRhiD3D11InitParams params;
    std::unique_ptr<QRhi> rhi{QRhi::create(QRhi::D3D11, &params, QRhi::PreferSoftwareRenderer)};
#else
    std::cout << "Pixel checks require Metal or D3D11 on this standalone harness\n";
    return 77;
#endif
#if defined(__APPLE__) || defined(_WIN32)
    if(!rhi) throw std::runtime_error("Pixel-check graphics backend unavailable");
    const auto vs = bake(productionLiteral("/Gfx/Graph/Mesh.cpp", "const char* TexturedMesh::defaultVertexShader()"), QShader::VertexStage);
    const auto fs = bake(productionLiteral("/Gfx/Graph/MultiWindowNode.cpp", "static const constexpr auto frag_shader"), QShader::FragmentStage);
    checkPixels(rhi.get(), vs, fs, false);
    checkPixels(rhi.get(), vs, fs, true);
    std::cout << "Production shader pixels passed: mesh crop, RGB calibration, neutral HDR\n";
#endif
  }
  catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
