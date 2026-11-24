#pragma once
#include <QImage>

namespace Gfx
{
enum class PreviewContent
{
  LiveInput = -1,
  Black, PerOutputTestCard, GlobalTestCard, OutputIdentification,
  GlobalText, White, Gray, Red, Green, Blue
};
// Unwarped source pixels. The production output applies warp, blend and gains.
QImage renderCalibrationPattern(PreviewContent content, QSize size, int outputIndex);
QImage renderTextTestCard(int w, int h);
QImage renderTestCard(int w, int h);
}
