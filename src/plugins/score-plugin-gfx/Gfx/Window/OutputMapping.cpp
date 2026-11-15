#include <score_plugin_gfx_export.h>
#include "OutputMapping.hpp"
#include <score/serialization/DataStreamVisitor.hpp>
#include <score/serialization/JSONParse.hpp>
#include <score/serialization/JSONVisitor.hpp>
#include <array>
#include <string>
#include <algorithm>

template <>
SCORE_PLUGIN_GFX_EXPORT void JSONReader::read(const Gfx::OutputMapping& n)
{
  stream.StartObject();
  stream.Key("SourceRect");
  stream.StartArray();
  stream.Double(n.sourceRect.x());
  stream.Double(n.sourceRect.y());
  stream.Double(n.sourceRect.width());
  stream.Double(n.sourceRect.height());
  stream.EndArray();
  stream.Key("ScreenIndex");
  stream.Int(n.screenIndex);
  stream.Key("WindowPosition");
  stream.StartArray();
  stream.Int(n.windowPosition.x());
  stream.Int(n.windowPosition.y());
  stream.EndArray();
  stream.Key("WindowSize");
  stream.StartArray();
  stream.Int(n.windowSize.width());
  stream.Int(n.windowSize.height());
  stream.EndArray();
  stream.Key("Fullscreen");
  stream.Bool(n.fullscreen);

  auto writeBlend = [&](const char* key, const Gfx::EdgeBlend& b) {
    stream.Key(key);
    stream.StartArray();
    stream.Double(b.width);
    stream.Double(b.gamma);
    stream.EndArray();
  };
  writeBlend("BlendLeft", n.blendLeft);
  writeBlend("BlendRight", n.blendRight);
  writeBlend("BlendTop", n.blendTop);
  writeBlend("BlendBottom", n.blendBottom);

  if(!n.cornerWarp.isIdentity())
  {
    stream.Key("CornerWarp");
    stream.StartArray();
    stream.Double(n.cornerWarp.topLeft.x());
    stream.Double(n.cornerWarp.topLeft.y());
    stream.Double(n.cornerWarp.topRight.x());
    stream.Double(n.cornerWarp.topRight.y());
    stream.Double(n.cornerWarp.bottomLeft.x());
    stream.Double(n.cornerWarp.bottomLeft.y());
    stream.Double(n.cornerWarp.bottomRight.x());
    stream.Double(n.cornerWarp.bottomRight.y());
    stream.EndArray();
  }

  if(!n.calibration.isIdentity())
  {
    stream.Key("Calibration"); stream.StartArray();
    stream.Double(n.calibration.red); stream.Double(n.calibration.green); stream.Double(n.calibration.blue);
    stream.Double(n.calibration.gamma); stream.Double(n.calibration.blackLevel);
    stream.EndArray();
  }

  if(n.meshWarp.enabled())
  {
    stream.Key("MeshWarp");
    stream.StartObject();
    stream.Key("Columns"); stream.Int(n.meshWarp.columns);
    stream.Key("Rows"); stream.Int(n.meshWarp.rows);
    stream.Key("Points"); stream.StartArray();
    for(const auto& point : n.meshWarp.points)
    {
      stream.StartArray(); stream.Double(point.x()); stream.Double(point.y()); stream.EndArray();
    }
    stream.EndArray(); stream.EndObject();
  }

  if(n.lockMode != Gfx::OutputLockMode::Free)
  {
    stream.Key("LockMode");
    stream.Int(int(n.lockMode));
  }

  if(n.rotation != 0)
  {
    stream.Key("Rotation");
    stream.Int(n.rotation);
  }
  if(n.mirrorX)
  {
    stream.Key("MirrorX");
    stream.Bool(n.mirrorX);
  }
  if(n.mirrorY)
  {
    stream.Key("MirrorY");
    stream.Bool(n.mirrorY);
  }

  stream.EndObject();
}

template <>
SCORE_PLUGIN_GFX_EXPORT void DataStreamReader::read(const Gfx::OutputMapping& n)
{
  m_stream << n.sourceRect << n.screenIndex << n.windowPosition << n.windowSize
           << n.fullscreen;
  m_stream << n.blendLeft.width << n.blendLeft.gamma;
  m_stream << n.blendRight.width << n.blendRight.gamma;
  m_stream << n.blendTop.width << n.blendTop.gamma;
  m_stream << n.blendBottom.width << n.blendBottom.gamma;
  m_stream << n.cornerWarp.topLeft << n.cornerWarp.topRight << n.cornerWarp.bottomLeft
           << n.cornerWarp.bottomRight;
  m_stream << int(n.lockMode);
  // A negative rotation is an extension marker; original 3.9.0 records retain their layout.
  m_stream << ((n.meshWarp.enabled() || !n.calibration.isIdentity()) ? -1 : n.rotation) << n.mirrorX << n.mirrorY;
  if(n.meshWarp.enabled() || !n.calibration.isIdentity())
  {
    const bool hasMesh = n.meshWarp.enabled();
    m_stream << n.rotation << (hasMesh ? n.meshWarp.columns : 0) << (hasMesh ? n.meshWarp.rows : 0);
    if(hasMesh)
      for(const auto& point : n.meshWarp.points) m_stream << point;
    m_stream << n.calibration.red << n.calibration.green << n.calibration.blue << n.calibration.gamma << n.calibration.blackLevel;
  }
}

template <>
SCORE_PLUGIN_GFX_EXPORT void DataStreamWriter::write(Gfx::OutputMapping& n)
{
  m_stream >> n.sourceRect >> n.screenIndex >> n.windowPosition >> n.windowSize
      >> n.fullscreen;
  m_stream >> n.blendLeft.width >> n.blendLeft.gamma;
  m_stream >> n.blendRight.width >> n.blendRight.gamma;
  m_stream >> n.blendTop.width >> n.blendTop.gamma;
  m_stream >> n.blendBottom.width >> n.blendBottom.gamma;
  m_stream >> n.cornerWarp.topLeft >> n.cornerWarp.topRight >> n.cornerWarp.bottomLeft
      >> n.cornerWarp.bottomRight;
  { int lm{}; m_stream >> lm; n.lockMode = Gfx::OutputLockMode(lm); }
  m_stream >> n.rotation >> n.mirrorX >> n.mirrorY;
  if(n.rotation == -1)
  {
    m_stream >> n.rotation >> n.meshWarp.columns >> n.meshWarp.rows;
    if(!((n.meshWarp.columns == 0 && n.meshWarp.rows == 0)
         || (n.meshWarp.columns >= 2 && n.meshWarp.columns <= 17 && n.meshWarp.rows >= 2 && n.meshWarp.rows <= 17)))
    {
      m_stream.stream.setStatus(QDataStream::ReadCorruptData);
      n.meshWarp = {};
      return;
    }
    n.meshWarp.points.resize(n.meshWarp.columns * n.meshWarp.rows);
    for(auto& point : n.meshWarp.points) m_stream >> point;
    if(n.meshWarp.enabled() && !n.meshWarp.hasValidCells())
    { m_stream.stream.setStatus(QDataStream::ReadCorruptData); n.meshWarp = {}; return; }
    m_stream >> n.calibration.red >> n.calibration.green >> n.calibration.blue >> n.calibration.gamma >> n.calibration.blackLevel;
    const auto& c = n.calibration;
    if(!std::isfinite(c.red) || !std::isfinite(c.green) || !std::isfinite(c.blue)
       || !std::isfinite(c.gamma) || !std::isfinite(c.blackLevel)
       || c.red < 0.f || c.red > 2.f || c.green < 0.f || c.green > 2.f || c.blue < 0.f || c.blue > 2.f
       || c.gamma < .1f || c.gamma > 4.f || c.blackLevel < 0.f || c.blackLevel > .25f)
    { m_stream.stream.setStatus(QDataStream::ReadCorruptData); n.calibration = {}; }
  }
}
namespace
{
//! rapidjson's GetArray/GetInt/GetDouble/GetBool are asserts, and an
//! OutputMapping can arrive from a script as well as from a saved document.
template <std::size_t N, typename Obj>
bool readNumbers(const Obj& obj, const std::string& key, std::array<double, N>& out)
{
  auto v = obj.tryGet(key);
  if(!v || !v->obj.IsArray())
    return false;
  const auto& arr = v->obj.GetArray();
  if(arr.Size() != N)
    return false;
  for(rapidjson::SizeType i = 0; i < N; i++)
  {
    if(!arr[i].IsNumber())
      return false;
    out[i] = arr[i].GetDouble();
  }
  return true;
}
}

template <>
SCORE_PLUGIN_GFX_EXPORT void JSONWriter::write(Gfx::OutputMapping& n)
{
  if(std::array<double, 4> r; readNumbers(obj, "SourceRect", r))
    n.sourceRect = QRectF(r[0], r[1], r[2], r[3]);

  score::parseJsonField(obj, "ScreenIndex", n.screenIndex);

  if(std::array<double, 2> p; readNumbers(obj, "WindowPosition", p))
    n.windowPosition = QPoint(int(p[0]), int(p[1]));
  if(std::array<double, 2> p; readNumbers(obj, "WindowSize", p))
    n.windowSize = QSize(int(p[0]), int(p[1]));

  score::parseJsonField(obj, "Fullscreen", n.fullscreen);

  auto readBlend = [&](const std::string& key, Gfx::EdgeBlend& b) {
    if(std::array<double, 2> a; readNumbers(obj, key, a))
    {
      b.width = float(a[0]);
      b.gamma = float(a[1]);
    }
  };
  readBlend("BlendLeft", n.blendLeft);
  readBlend("BlendRight", n.blendRight);
  readBlend("BlendTop", n.blendTop);
  readBlend("BlendBottom", n.blendBottom);

  if(std::array<double, 8> c; readNumbers(obj, "CornerWarp", c))
  {
    n.cornerWarp.topLeft = {c[0], c[1]};
    n.cornerWarp.topRight = {c[2], c[3]};
    n.cornerWarp.bottomLeft = {c[4], c[5]};
    n.cornerWarp.bottomRight = {c[6], c[7]};
  }

  if(std::array<double, 5> c; readNumbers(obj, "Calibration", c))
  {
    if(std::all_of(c.begin(), c.end(), [](double v) { return std::isfinite(v); }))
      n.calibration = {float(qBound(0., c[0], 2.)), float(qBound(0., c[1], 2.)), float(qBound(0., c[2], 2.)),
                       float(qBound(.1, c[3], 4.)), float(qBound(0., c[4], .25))};
  }

  if(auto mesh = obj.tryGet("MeshWarp"); mesh && mesh->obj.IsObject())
  {
    const auto& value = mesh->obj;
    auto cols = value.FindMember("Columns"), rows = value.FindMember("Rows"), points = value.FindMember("Points");
    if(cols != value.MemberEnd() && rows != value.MemberEnd() && points != value.MemberEnd()
       && cols->value.IsInt() && rows->value.IsInt() && points->value.IsArray())
    {
      Gfx::MeshWarp candidate;
      candidate.columns = cols->value.GetInt(); candidate.rows = rows->value.GetInt();
      if(candidate.columns >= 2 && candidate.columns <= 17 && candidate.rows >= 2 && candidate.rows <= 17
         && points->value.Size() == unsigned(candidate.columns * candidate.rows))
      {
        for(const auto& point : points->value.GetArray())
        {
          if(!point.IsArray() || point.Size() != 2 || !point[0].IsNumber() || !point[1].IsNumber()
             || !std::isfinite(point[0].GetDouble()) || !std::isfinite(point[1].GetDouble()))
          { candidate.points.clear(); break; }
          candidate.points.emplace_back(point[0].GetDouble(), point[1].GetDouble());
        }
        if(candidate.hasValidCells()) n.meshWarp = std::move(candidate);
      }
    }
  }

  if(!score::parseJsonField(obj, "LockMode", n.lockMode))
  {
    // Backward compatibility with old bool fields
    bool lockSize = false, locked = false;
    score::parseJsonField(obj, "LockSizeToInput", lockSize);
    score::parseJsonField(obj, "Locked", locked);
    if(locked)
      n.lockMode = Gfx::OutputLockMode::FullLock;
    else if(lockSize)
      n.lockMode = Gfx::OutputLockMode::OneToOne;
  }

  score::parseJsonField(obj, "Rotation", n.rotation);
  score::parseJsonField(obj, "MirrorX", n.mirrorX);
  score::parseJsonField(obj, "MirrorY", n.mirrorY);
}
