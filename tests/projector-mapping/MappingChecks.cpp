#include <Gfx/Window/OutputMapping.hpp>
#include <Gfx/Window/OutputPreview.hpp>
#include <Gfx/Window/TestCard.hpp>
#include <QApplication>
#include <QTest>
#include <QDir>
#include <iostream>
#include <stdexcept>

static void check(bool result, const char* message)
{
  if(!result) throw std::runtime_error(message);
}

static QImage paint(Gfx::PreviewWidget& widget)
{
  QImage image(widget.size(), QImage::Format_RGB32);
  widget.render(&image);
  return image;
}

int main(int argc, char** argv)
{
  QApplication app(argc, argv);
  try
  {
    using namespace Gfx;
    check(renderCalibrationPattern(PreviewContent::LiveInput, {64, 32}, 0).isNull(),
          "Live input does not allocate a test image");
    const std::pair<PreviewContent, QColor> solids[] = {
      {PreviewContent::Black, Qt::black}, {PreviewContent::White, Qt::white},
      {PreviewContent::Gray, QColor(128, 128, 128)}, {PreviewContent::Red, Qt::red},
      {PreviewContent::Green, Qt::green}, {PreviewContent::Blue, Qt::blue}};
    for(const auto& [mode, color] : solids)
      check(renderCalibrationPattern(mode, {32, 16}, 0).pixelColor(4, 4) == color,
            "Production calibration texture has the selected solid color");
    check(renderCalibrationPattern(PreviewContent::GlobalText, {5280, 1080}, 0).size()
              == QSize(5280, 1080), "Production global text uses the shared canvas size");
    check(renderCalibrationPattern(PreviewContent::OutputIdentification, {192, 144}, 0)
              != renderCalibrationPattern(PreviewContent::OutputIdentification, {192, 144}, 1),
          "Production identification textures distinguish outputs");
    OutputMapping legacy;
    check(!legacy.meshWarp.enabled(), "Legacy mappings must keep four-corner mode");
    check(legacy.calibration.isIdentity(), "Legacy colour calibration must be neutral");
    MeshWarp mesh;
    mesh.reset(5, 7, {});
    check(mesh.hasValidCells() && mesh.points.size() == 35, "Rectangular mesh dimensions");
    check(mesh.points.front() == QPointF(0, 0) && mesh.points.back() == QPointF(1, 1), "Identity mesh corners");
    mesh.points[17] += QPointF(.02, .01);
    check(mesh.hasValidCells(), "Interior control point adjustment");
    mesh.points[17] = QPointF(-2, -2);
    check(!mesh.hasValidCells(), "Folded mesh must be rejected");
    CornerWarp corners;
    corners.topLeft = {.1, .1}; corners.bottomRight = {.9, .9};
    mesh.reset(17, 17, corners);
    check(mesh.hasValidCells() && mesh.points.front() == corners.topLeft, "Mesh initialized from corner calibration");

    OutputMappingCanvas canvas;
    std::vector<OutputMapping> mappings(3);
    for(int i = 0; i < 3; ++i) mappings[i].sourceRect = QRectF(i * .3, 0., .4, 1.);
    canvas.setMappings(mappings);
    canvas.autoBlendOverlaps();
    auto blended = canvas.getMappings();
    check(qAbs(blended[0].blendRight.width - .25f) < 1e-5, "First overlap width");
    check(blended[0].blendLeft.width == 0.f && blended[2].blendRight.width == 0.f, "Outer edges remain unblended");
    check(qAbs(blended[1].blendLeft.width - .25f) < 1e-5 && qAbs(blended[1].blendRight.width - .25f) < 1e-5, "Middle projector blends on both sides");
    canvas.enterWarpMode(1);
    canvas.setMeshResolution(7, 5);
    auto meshed = canvas.getMappings();
    check(meshed[1].meshWarp.points.size() == 35 && canvas.inWarpMode(), "Editor mesh creation");
    canvas.resetWarp();
    canvas.exitWarpMode();
    canvas.setMappings(meshed);
    check(canvas.getMappings()[1].meshWarp.points == meshed[1].meshWarp.points, "Mesh survives canvas reload");

    canvas.setMappings({OutputMapping{}});
    canvas.resize(900, 600);
    canvas.show();
    app.processEvents();
    canvas.enterWarpMode(0);
    canvas.setMeshResolution(5, 5);
    const QPoint center = canvas.mapFromScene(QPointF(canvas.canvasWidth() * .5, canvas.canvasHeight() * .5));
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, center);
    QTest::mouseMove(canvas.viewport(), center + QPoint(12, 0));
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, center + QPoint(12, 0));
    check(canvas.getMappings()[0].meshWarp.points[12].x() > .5, "Dragging an interior mesh handle changes calibration");
    canvas.resetWarp();
    check(canvas.getMappings()[0].meshWarp.points[12] == QPointF(.5, .5), "Reset restores regular control points");
    canvas.setMappings({OutputMapping{}});
    check(!canvas.inWarpMode(), "Replacing mappings cleans up warp handles");
    canvas.hide();

    PreviewWidget preview(0, PreviewContent::White);
    preview.resize(640, 360);
    auto white = paint(preview);
    check(qRed(white.pixel(320, 180)) == 255, "Solid white test field");
    preview.setPreviewContent(PreviewContent::Gray);
    check(qRed(paint(preview).pixel(320, 180)) == 128, "Mid-gray test field");
    preview.setPreviewContent(PreviewContent::Red);
    check(qRed(paint(preview).pixel(320, 180)) == 255 && qGreen(paint(preview).pixel(320, 180)) == 0, "Red test field");
    preview.setPreviewContent(PreviewContent::White);
    preview.setBlend({}, {.25f, 1.f}, {}, {});
    auto ramp = paint(preview);
    check(qRed(ramp.pixel(639, 180)) < 5 && qRed(ramp.pixel(320, 180)) == 255, "Blend reaches edge without bright fringe");
    preview.setBlend({}, {}, {}, {});
    preview.setCalibration({.5f, 1.f, 1.f, 1.f, 0.f});
    auto corrected = paint(preview);
    check(qAbs(qRed(corrected.pixel(320, 180)) - 128) <= 1 && qGreen(corrected.pixel(320, 180)) == 255, "Per-projector RGB correction");
    preview.setCalibration({1.f, 1.f, 1.f, 1.f, .1f});
    preview.setPreviewContent(PreviewContent::Black);
    check(qAbs(qRed(paint(preview).pixel(320, 180)) - 26) <= 1, "Black floor compensation");
    preview.setPreviewContent(PreviewContent::White);
    preview.setCalibration({});
    mesh.reset(5, 5, {});
    for(auto& p : mesh.points) p.setX(p.x() * .75);
    preview.setMeshWarp(mesh);
    auto warped = paint(preview);
    check(qRed(warped.pixel(400, 180)) == 255 && qRed(warped.pixel(600, 180)) == 0, "Preview follows actual mesh geometry");
    for(int y = 1; y < 359; ++y)
      for(int x = 1; x < 479; ++x)
        check(qRed(warped.pixel(x, y)) == 255, "Mesh triangles must not introduce dark cracks");
    preview.setMeshWarp({});
    auto text = renderTextTestCard(1920, 360);
    check(!text.isNull(), "Global text pattern generated");
    int bright = 0;
    for(int y = 0; y < text.height(); ++y)
      for(int x = 0; x < text.width(); ++x)
        if(qRed(text.pixel(x, y)) > 200) ++bright;
    check(bright > 10000, "Text fills the wide focus field");
    if(argc > 1)
    {
      QDir().mkpath(argv[1]);
      text.save(QString::fromLocal8Bit(argv[1]) + "/text-focus.png");
      warped.save(QString::fromLocal8Bit(argv[1]) + "/mesh-preview.png");
      ramp.save(QString::fromLocal8Bit(argv[1]) + "/edge-blend.png");
    }
    std::cout << "Projector mapping checks passed\n";
  }
  catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
