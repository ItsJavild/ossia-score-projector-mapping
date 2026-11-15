#include "OutputMapping.hpp"


#include <array>
#include <string>

#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QMouseEvent>

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPolygonItem>
#include <QCursor>
#include <algorithm>

namespace Gfx
{

// --- OutputMappingItem implementation ---

OutputMappingItem::OutputMappingItem(
    int index, const QRectF& rect, OutputMappingCanvas* canvas,
    QGraphicsItem* parent)
    : QGraphicsRectItem(rect, parent)
    , m_index{index}
    , m_canvas{canvas}
{
  setFlags(
      QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable
      | QGraphicsItem::ItemSendsGeometryChanges);
  setAcceptHoverEvents(true);
  setPen(QPen(Qt::white, 1));
  setBrush(QBrush(QColor(100, 150, 255, 80)));
}

void OutputMappingItem::setOutputIndex(int idx)
{
  m_index = idx;
  update();
}

void OutputMappingItem::applyLockedState()
{
  if(lockMode == OutputLockMode::FullLock)
  {
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemSendsGeometryChanges);
    setAcceptHoverEvents(false);
    // The view applies item->cursor() on mouse-over even with hover events
    // off, so a resize cursor left over from the unlocked state would stick.
    setCursor(Qt::ArrowCursor);
  }
  else
  {
    setFlags(
        QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable
        | QGraphicsItem::ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
  }
  update();
}

void OutputMappingItem::paint(
    QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
  // Selected items get brighter fill and come to foreground
  const bool isLocked = (lockMode == OutputLockMode::FullLock);
  if(isSelected())
  {
    setZValue(20);
    if(isLocked)
      setBrush(QBrush(QColor(180, 200, 255, 100)));
    else
      setBrush(QBrush(QColor(140, 190, 255, 120)));
    setPen(QPen(Qt::yellow, 2));
  }
  else
  {
    setZValue(10);
    if(isLocked)
      setBrush(QBrush(QColor(80, 80, 80, 60)));
    else
      setBrush(QBrush(QColor(100, 150, 255, 80)));
    setPen(QPen(Qt::white, 1));
  }

  QGraphicsRectItem::paint(painter, option, widget);

  // Draw index label
  painter->setPen(isLocked ? QColor(180, 180, 180) : QColor(Qt::white));
  auto font = painter->font();
  font.setPixelSize(14);
  painter->setFont(font);
  QString label = QString::number(m_index);
  static constexpr const char* lockLabels[]
      = {nullptr, " [AR]", " [1:1]", " [L]"};
  if(int(lockMode) > 0 && int(lockMode) <= 3)
    label += QString::fromLatin1(lockLabels[int(lockMode)]);
  painter->drawText(rect(), Qt::AlignCenter, label);

  // Draw blend zones as gradient overlays
  auto r = rect();
  auto drawBlendZone = [&](const EdgeBlend& blend, const QRectF& zone, bool horizontal) {
    if(blend.width <= 0.0f)
      return;
    QLinearGradient grad;
    QColor blendColor(255, 200, 50, 100);
    QColor transparent(255, 200, 50, 0);
    if(horizontal)
    {
      grad.setStart(zone.left(), zone.center().y());
      grad.setFinalStop(zone.right(), zone.center().y());
    }
    else
    {
      grad.setStart(zone.center().x(), zone.top());
      grad.setFinalStop(zone.center().x(), zone.bottom());
    }
    grad.setColorAt(0, blendColor);
    grad.setColorAt(1, transparent);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QBrush(grad));
    painter->drawRect(zone);
  };

  // Left blend: gradient from left edge inward
  if(blendLeft.width > 0.0f)
  {
    double w = blendLeft.width * r.width();
    drawBlendZone(blendLeft, QRectF(r.left(), r.top(), w, r.height()), true);
  }
  // Right blend: gradient from right edge inward
  if(blendRight.width > 0.0f)
  {
    double w = blendRight.width * r.width();
    QLinearGradient grad(r.right(), r.center().y(), r.right() - w, r.center().y());
    grad.setColorAt(0, QColor(255, 200, 50, 100));
    grad.setColorAt(1, QColor(255, 200, 50, 0));
    painter->setPen(Qt::NoPen);
    painter->setBrush(QBrush(grad));
    painter->drawRect(QRectF(r.right() - w, r.top(), w, r.height()));
  }
  // Top blend: gradient from top edge inward
  if(blendTop.width > 0.0f)
  {
    double h = blendTop.width * r.height();
    drawBlendZone(blendTop, QRectF(r.left(), r.top(), r.width(), h), false);
  }
  // Bottom blend: gradient from bottom edge inward
  if(blendBottom.width > 0.0f)
  {
    double h = blendBottom.width * r.height();
    QLinearGradient grad(r.center().x(), r.bottom(), r.center().x(), r.bottom() - h);
    grad.setColorAt(0, QColor(255, 200, 50, 100));
    grad.setColorAt(1, QColor(255, 200, 50, 0));
    painter->setPen(Qt::NoPen);
    painter->setBrush(QBrush(grad));
    painter->drawRect(QRectF(r.left(), r.bottom() - h, r.width(), h));
  }

  // Draw blend handle lines at the boundary of each blend zone.
  // Always draw a subtle dotted line at the handle position (even when width=0)
  // so the user knows the handle is grabbable; draw a stronger line when active.
  {
    constexpr double minInset = 10.0;
    painter->setBrush(Qt::NoBrush);

    auto drawHandle = [&](float blendWidth, bool horizontal, bool fromStart) {
      double offset;
      if(horizontal)
        offset = std::max((double)blendWidth * r.width(), minInset);
      else
        offset = std::max((double)blendWidth * r.height(), minInset);

      // Active blend: bright dashed line; inactive: dim dotted line
      if(blendWidth > 0.0f)
        painter->setPen(QPen(QColor(255, 200, 50, 200), 1.5, Qt::DashLine));
      else
        painter->setPen(QPen(QColor(255, 200, 50, 60), 1, Qt::DotLine));

      if(horizontal)
      {
        double x = fromStart ? r.left() + offset : r.right() - offset;
        painter->drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
      }
      else
      {
        double y = fromStart ? r.top() + offset : r.bottom() - offset;
        painter->drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
      }
    };

    drawHandle(blendLeft.width, true, true);
    drawHandle(blendRight.width, true, false);
    drawHandle(blendTop.width, false, true);
    drawHandle(blendBottom.width, false, false);
  }

}

int OutputMappingItem::hitTestEdges(const QPointF& pos) const
{
  constexpr double margin = 6.0;
  int edges = None;
  auto r = rect();
  if(pos.x() - r.left() < margin)
    edges |= Left;
  if(r.right() - pos.x() < margin)
    edges |= Right;
  if(pos.y() - r.top() < margin)
    edges |= Top;
  if(r.bottom() - pos.y() < margin)
    edges |= Bottom;
  return edges;
}

OutputMappingItem::BlendHandle
OutputMappingItem::hitTestBlendHandles(const QPointF& pos) const
{
  constexpr double handleMargin = 5.0;
  // Minimum inset from the edge so the handle zone never overlaps with the
  // 6px resize margin. This ensures handles are grabbable even at width=0.
  constexpr double minInset = 10.0;
  auto r = rect();

  {
    double handleX = r.left() + std::max((double)blendLeft.width * r.width(), minInset);
    if(std::abs(pos.x() - handleX) < handleMargin && pos.y() >= r.top()
       && pos.y() <= r.bottom())
      return BlendLeft;
  }
  {
    double handleX
        = r.right() - std::max((double)blendRight.width * r.width(), minInset);
    if(std::abs(pos.x() - handleX) < handleMargin && pos.y() >= r.top()
       && pos.y() <= r.bottom())
      return BlendRight;
  }
  {
    double handleY = r.top() + std::max((double)blendTop.width * r.height(), minInset);
    if(std::abs(pos.y() - handleY) < handleMargin && pos.x() >= r.left()
       && pos.x() <= r.right())
      return BlendTop;
  }
  {
    double handleY
        = r.bottom() - std::max((double)blendBottom.width * r.height(), minInset);
    if(std::abs(pos.y() - handleY) < handleMargin && pos.x() >= r.left()
       && pos.x() <= r.right())
      return BlendBottom;
  }
  return BlendNone;
}

QVariant OutputMappingItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
  if(change == ItemPositionChange && scene())
  {
    QPointF newPos = value.toPointF();

    // Snap before clamping
    if(m_canvas && m_canvas->snapEnabled())
      newPos = m_canvas->snapPosition(this, newPos);

    // Clamp to canvas bounds (not scene rect, which includes margin for warp
    // handles). Optional: with "Lock to border" off the quad may go before
    // (0,0) and past (1,1).
    if(!m_canvas || m_canvas->lockToBorderEnabled())
    {
      auto r = rect();
      QRectF sr(0, 0, m_canvas ? m_canvas->canvasWidth() : 400, m_canvas ? m_canvas->canvasHeight() : 300);

      double minX = sr.left() - r.left();
      double maxX = sr.right() - r.right();
      double minY = sr.top() - r.top();
      double maxY = sr.bottom() - r.bottom();
      newPos.setX(qBound(std::min(minX, maxX), newPos.x(), std::max(minX, maxX)));
      newPos.setY(qBound(std::min(minY, maxY), newPos.y(), std::max(minY, maxY)));
    }
    return newPos;
  }
  if(change == ItemPositionHasChanged)
  {
    if(onChanged)
      onChanged();
  }
  return QGraphicsRectItem::itemChange(change, value);
}

void OutputMappingItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
  if(lockMode == OutputLockMode::FullLock)
  {
    // Allow selection but not movement
    QGraphicsRectItem::mousePressEvent(event);
    return;
  }

  // Check blend handles first (they are inside the rect, so test before edges)
  m_blendHandle = hitTestBlendHandles(event->pos());
  if(m_blendHandle != BlendNone)
  {
    m_dragStart = event->pos();
    event->accept();
    return;
  }

  m_resizeEdges = hitTestEdges(event->pos());
  if(m_resizeEdges != None)
  {
    m_dragStart = event->scenePos();
    m_rectStart = rect();
    event->accept();
  }
  else
  {
    // Store anchor for precision move (Ctrl)
    m_moveAnchorScene = event->scenePos();
    m_posAtPress = pos();
    QGraphicsRectItem::mousePressEvent(event);
  }
}

void OutputMappingItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
  if(lockMode == OutputLockMode::FullLock)
  {
    event->accept();
    return;
  }

  const double scale = (event->modifiers() & Qt::ControlModifier) ? 0.1 : 1.0;

  if(m_blendHandle != BlendNone)
  {
    auto r = rect();
    // Apply precision: scale the delta from drag start
    auto rawPos = event->pos();
    auto pos = m_dragStart + (rawPos - m_dragStart) * scale;

    switch(m_blendHandle)
    {
      case BlendLeft: {
        double newW = qBound(0.0, (pos.x() - r.left()) / r.width(), 0.5);
        blendLeft.width = (float)newW;
        break;
      }
      case BlendRight: {
        double newW = qBound(0.0, (r.right() - pos.x()) / r.width(), 0.5);
        blendRight.width = (float)newW;
        break;
      }
      case BlendTop: {
        double newH = qBound(0.0, (pos.y() - r.top()) / r.height(), 0.5);
        blendTop.width = (float)newH;
        break;
      }
      case BlendBottom: {
        double newH = qBound(0.0, (r.bottom() - pos.y()) / r.height(), 0.5);
        blendBottom.width = (float)newH;
        break;
      }
      default:
        break;
    }

    update();
    if(onChanged)
      onChanged();
    event->accept();
    return;
  }

  if(m_resizeEdges != None)
  {
    auto rawDelta = event->scenePos() - m_dragStart;
    auto delta = rawDelta * scale;
    QRectF r = m_rectStart;
    constexpr double minSize = 10.0;

    // Scene bounds in item-local coordinates (account for pos() offset)
    auto sr = scene()->sceneRect();
    auto p = pos();
    double sLeft = sr.left() - p.x();
    double sRight = sr.right() - p.x();
    double sTop = sr.top() - p.y();
    double sBottom = sr.bottom() - p.y();

    if(m_resizeEdges & Left)
    {
      double newLeft = qMin(r.left() + delta.x(), r.right() - minSize);
      r.setLeft(qMax(newLeft, sLeft));
    }
    if(m_resizeEdges & Right)
    {
      double newRight = qMax(r.right() + delta.x(), r.left() + minSize);
      r.setRight(qMin(newRight, sRight));
    }
    if(m_resizeEdges & Top)
    {
      double newTop = qMin(r.top() + delta.y(), r.bottom() - minSize);
      r.setTop(qMax(newTop, sTop));
    }
    if(m_resizeEdges & Bottom)
    {
      double newBottom = qMax(r.bottom() + delta.y(), r.top() + minSize);
      r.setBottom(qMin(newBottom, sBottom));
    }

    setRect(r);
    if(onChanged)
      onChanged();
    event->accept();
  }
  else
  {
    // Precision move: bypass default handler, compute position manually
    auto rawDelta = event->scenePos() - m_moveAnchorScene;
    auto delta = rawDelta * scale;
    setPos(m_posAtPress + delta);
  }
}

void OutputMappingItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
  m_blendHandle = BlendNone;
  m_resizeEdges = None;
  QGraphicsRectItem::mouseReleaseEvent(event);
}

void OutputMappingItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event)
{
  if(lockMode == OutputLockMode::FullLock)
  {
    setCursor(Qt::ArrowCursor);
    QGraphicsRectItem::hoverMoveEvent(event);
    return;
  }

  // Check blend handles first
  auto bh = hitTestBlendHandles(event->pos());
  if(bh == BlendLeft || bh == BlendRight)
  {
    setCursor(Qt::SplitHCursor);
    QGraphicsRectItem::hoverMoveEvent(event);
    return;
  }
  if(bh == BlendTop || bh == BlendBottom)
  {
    setCursor(Qt::SplitVCursor);
    QGraphicsRectItem::hoverMoveEvent(event);
    return;
  }

  int edges = hitTestEdges(event->pos());
  if((edges & Left) && (edges & Top))
    setCursor(Qt::SizeFDiagCursor);
  else if((edges & Right) && (edges & Bottom))
    setCursor(Qt::SizeFDiagCursor);
  else if((edges & Left) && (edges & Bottom))
    setCursor(Qt::SizeBDiagCursor);
  else if((edges & Right) && (edges & Top))
    setCursor(Qt::SizeBDiagCursor);
  else if(edges & (Left | Right))
    setCursor(Qt::SizeHorCursor);
  else if(edges & (Top | Bottom))
    setCursor(Qt::SizeVerCursor);
  else
    setCursor(Qt::ArrowCursor);

  QGraphicsRectItem::hoverMoveEvent(event);
}

// --- OutputMappingCanvas implementation ---

OutputMappingCanvas::OutputMappingCanvas(QWidget* parent)
    : QGraphicsView(parent)
{
  setScene(&m_scene);
  constexpr double margin = 30.0;
  m_scene.setSceneRect(
      -margin, -margin, m_canvasWidth + 2 * margin, m_canvasHeight + 2 * margin);
  setMinimumSize(200, 150);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setBackgroundBrush(QBrush(QColor(40, 40, 40)));
  setRenderHint(QPainter::Antialiasing);

  // Draw border around the full texture area
  m_border = m_scene.addRect(0, 0, m_canvasWidth, m_canvasHeight, QPen(Qt::gray, 1));
  m_border->setZValue(-1);

  connect(&m_scene, &QGraphicsScene::selectionChanged, this, [this] {
    if(onSelectionChanged)
    {
      auto items = m_scene.selectedItems();
      if(!items.isEmpty())
      {
        if(auto* item = dynamic_cast<OutputMappingItem*>(items.first()))
          onSelectionChanged(item->outputIndex());
      }
      else
      {
        onSelectionChanged(-1);
      }
    }
  });
}

OutputMappingCanvas::~OutputMappingCanvas()
{
  // Clear callbacks before the scene destroys items (which triggers selectionChanged)
  onSelectionChanged = nullptr;
  onItemGeometryChanged = nullptr;
}

void OutputMappingCanvas::resizeEvent(QResizeEvent* event)
{
  QGraphicsView::resizeEvent(event);
  fitInView(m_scene.sceneRect(), Qt::KeepAspectRatio);
}

void OutputMappingCanvas::updateAspectRatio(int inputWidth, int inputHeight)
{
  if(inputWidth < 1 || inputHeight < 1)
    return;

  // Keep the longer axis at 400px, scale the other to match the aspect ratio
  constexpr double maxDim = 400.0;
  double aspect = (double)inputWidth / (double)inputHeight;
  double oldW = m_canvasWidth;
  double oldH = m_canvasHeight;

  if(aspect >= 1.0)
  {
    m_canvasWidth = maxDim;
    m_canvasHeight = maxDim / aspect;
  }
  else
  {
    m_canvasHeight = maxDim;
    m_canvasWidth = maxDim * aspect;
  }

  // Rescale existing mapping items from old dimensions to new
  if(oldW > 0 && oldH > 0)
  {
    double sx = m_canvasWidth / oldW;
    double sy = m_canvasHeight / oldH;

    for(auto* item : m_scene.items())
    {
      if(auto* mi = dynamic_cast<OutputMappingItem*>(item))
      {
        auto r = mi->mapRectToScene(mi->rect());
        QRectF scaled(r.x() * sx, r.y() * sy, r.width() * sx, r.height() * sy);
        mi->setPos(0, 0);
        mi->setRect(scaled);
      }
    }
  }

  constexpr double margin = 30.0;
  m_scene.setSceneRect(
      -margin, -margin, m_canvasWidth + 2 * margin, m_canvasHeight + 2 * margin);
  if(m_border)
    m_border->setRect(0, 0, m_canvasWidth, m_canvasHeight);
  fitInView(m_scene.sceneRect(), Qt::KeepAspectRatio);
}

void OutputMappingCanvas::setupItemCallbacks(OutputMappingItem* item)
{
  item->onChanged = [this, item] {
    if(onItemGeometryChanged)
      onItemGeometryChanged(item->outputIndex());
  };
}

void OutputMappingCanvas::setMappings(const std::vector<OutputMapping>& mappings)
{
  exitWarpMode();
  // Remove existing mapping items
  QList<QGraphicsItem*> toRemove;
  for(auto* item : m_scene.items())
  {
    if(dynamic_cast<OutputMappingItem*>(item))
      toRemove.append(item);
  }
  for(auto* item : toRemove)
  {
    m_scene.removeItem(item);
    delete item;
  }

  for(int i = 0; i < (int)mappings.size(); ++i)
  {
    const auto& m = mappings[i];
    QRectF sceneRect(
        m.sourceRect.x() * canvasWidth(), m.sourceRect.y() * canvasHeight(),
        m.sourceRect.width() * canvasWidth(), m.sourceRect.height() * canvasHeight());
    auto* item = new OutputMappingItem(i, sceneRect, this);
    item->screenIndex = m.screenIndex;
    item->windowPosition = m.windowPosition;
    item->windowSize = m.windowSize;
    item->fullscreen = m.fullscreen;
    item->blendLeft = m.blendLeft;
    item->blendRight = m.blendRight;
    item->blendTop = m.blendTop;
    item->blendBottom = m.blendBottom;
    item->cornerWarp = m.cornerWarp;
    item->meshWarp = m.meshWarp;
    item->calibration = m.calibration;
    item->lockMode = m.lockMode;
    item->rotation = m.rotation;
    item->mirrorX = m.mirrorX;
    item->mirrorY = m.mirrorY;
    item->applyLockedState();
    setupItemCallbacks(item);
    m_scene.addItem(item);
  }
}

std::vector<OutputMapping> OutputMappingCanvas::getMappings() const
{
  std::vector<OutputMapping> result;
  QList<OutputMappingItem*> items;

  for(auto* item : m_scene.items())
  {
    if(auto* mi = dynamic_cast<OutputMappingItem*>(item))
      items.append(mi);
  }

  // Sort by index
  std::sort(items.begin(), items.end(), [](auto* a, auto* b) {
    return a->outputIndex() < b->outputIndex();
  });

  for(auto* item : items)
  {
    OutputMapping m;
    auto r = item->mapRectToScene(item->rect());
    m.sourceRect = QRectF(
        r.x() / canvasWidth(), r.y() / canvasHeight(), r.width() / canvasWidth(),
        r.height() / canvasHeight());
    m.screenIndex = item->screenIndex;
    m.windowPosition = item->windowPosition;
    m.windowSize = item->windowSize;
    m.fullscreen = item->fullscreen;
    m.blendLeft = item->blendLeft;
    m.blendRight = item->blendRight;
    m.blendTop = item->blendTop;
    m.blendBottom = item->blendBottom;
    m.cornerWarp = item->cornerWarp;
    m.meshWarp = item->meshWarp;
    m.calibration = item->calibration;
    m.lockMode = item->lockMode;
    m.rotation = item->rotation;
    m.mirrorX = item->mirrorX;
    m.mirrorY = item->mirrorY;
    result.push_back(m);
  }
  return result;
}

void OutputMappingCanvas::addOutput()
{
  int count = 0;
  for(auto* item : m_scene.items())
    if(dynamic_cast<OutputMappingItem*>(item))
      count++;

  // Place new output at a default position. The ranges are 0 or negative
  // on a canvas smaller than the 100x75 item, where % would divide by zero.
  const int xRange = int(canvasWidth() - 100);
  const int yRange = int(canvasHeight() - 75);
  const double x = xRange > 0 ? (count * 30) % xRange : 0;
  const double y = yRange > 0 ? (count * 30) % yRange : 0;
  auto* item = new OutputMappingItem(count, QRectF(x, y, 100, 75), this);
  setupItemCallbacks(item);
  m_scene.addItem(item);

  // Auto-select the new item so that property auto-match runs
  m_scene.clearSelection();
  item->setSelected(true);
}

void OutputMappingCanvas::removeSelectedOutput()
{
  exitWarpMode();
  auto items = m_scene.selectedItems();
  for(auto* item : items)
  {
    if(dynamic_cast<OutputMappingItem*>(item))
    {
      m_scene.removeItem(item);
      delete item;
    }
  }

  // Re-index remaining items
  QList<OutputMappingItem*> remaining;
  for(auto* item : m_scene.items())
    if(auto* mi = dynamic_cast<OutputMappingItem*>(item))
      remaining.append(mi);

  std::sort(remaining.begin(), remaining.end(), [](auto* a, auto* b) {
    return a->outputIndex() < b->outputIndex();
  });

  for(int i = 0; i < remaining.size(); ++i)
    remaining[i]->setOutputIndex(i);
}

void OutputMappingCanvas::setSnapEnabled(bool enabled)
{
  m_snapEnabled = enabled;
}

void OutputMappingCanvas::setLockToBorderEnabled(bool enabled)
{
  m_lockToBorder = enabled;
}

QPointF OutputMappingCanvas::snapPosition(
    const OutputMappingItem* item, QPointF pos) const
{
  constexpr double threshold = 8.0;

  QRectF itemRect(pos, item->rect().size());

  // Collect snap edges: scene rect borders + other item borders
  std::vector<double> hEdges, vEdges;

  auto sr = m_scene.sceneRect();
  hEdges.push_back(sr.left());
  hEdges.push_back(sr.right());
  vEdges.push_back(sr.top());
  vEdges.push_back(sr.bottom());

  for(auto* gi : m_scene.items())
  {
    auto* other = dynamic_cast<const OutputMappingItem*>(gi);
    if(!other || other == item)
      continue;
    auto r = other->mapRectToScene(other->rect());
    hEdges.push_back(r.left());
    hEdges.push_back(r.right());
    vEdges.push_back(r.top());
    vEdges.push_back(r.bottom());
  }

  double bestDx = threshold + 1;
  double snapX = pos.x();
  for(double edge : hEdges)
  {
    double dLeft = std::abs(itemRect.left() - edge);
    double dRight = std::abs(itemRect.right() - edge);
    if(dLeft < bestDx)
    {
      bestDx = dLeft;
      snapX = edge;
    }
    if(dRight < bestDx)
    {
      bestDx = dRight;
      snapX = edge - itemRect.width();
    }
  }

  double bestDy = threshold + 1;
  double snapY = pos.y();
  for(double edge : vEdges)
  {
    double dTop = std::abs(itemRect.top() - edge);
    double dBottom = std::abs(itemRect.bottom() - edge);
    if(dTop < bestDy)
    {
      bestDy = dTop;
      snapY = edge;
    }
    if(dBottom < bestDy)
    {
      bestDy = dBottom;
      snapY = edge - itemRect.height();
    }
  }

  return QPointF(
      bestDx <= threshold ? snapX : pos.x(), bestDy <= threshold ? snapY : pos.y());
}

OutputMappingItem* OutputMappingCanvas::findItemByIndex(int index) const
{
  for(auto* gi : m_scene.items())
    if(auto* mi = dynamic_cast<OutputMappingItem*>(gi))
      if(mi->outputIndex() == index)
        return mi;
  return nullptr;
}

void OutputMappingCanvas::enterWarpMode(int outputIndex)
{
  if(m_warpItemIndex == outputIndex)
  {
    exitWarpMode();
    return;
  }
  exitWarpMode();

  auto* item = findItemByIndex(outputIndex);
  if(!item)
    return;

  m_warpItemIndex = outputIndex;

  // Disable move/resize on all items
  for(auto* gi : m_scene.items())
    if(auto* mi = dynamic_cast<OutputMappingItem*>(gi))
    {
      mi->setFlag(QGraphicsItem::ItemIsMovable, false);
      mi->setAcceptHoverEvents(false);
    }

  m_warpHandles.resize(item->meshWarp.enabled() ? item->meshWarp.points.size() : 4);

  // Create corner handles: TL, TR, BL, BR
  const double handleR = item->meshWarp.enabled()
      ? qMin(6., .3 * qMin(item->rect().width() / (item->meshWarp.columns - 1),
                            item->rect().height() / (item->meshWarp.rows - 1))) : 6.;
  const QColor handleColors[4]
      = {QColor(255, 80, 80), QColor(80, 255, 80), QColor(80, 80, 255),
         QColor(255, 255, 80)};

  for(int i = 0; i < int(m_warpHandles.size()); i++)
  {
    m_warpHandles[i] = m_scene.addEllipse(
        -handleR, -handleR, handleR * 2, handleR * 2, QPen(Qt::white, 1),
        QBrush(handleColors[i % 4]));
    m_warpHandles[i]->setZValue(100);
  }

  m_warpQuad = m_scene.addPolygon(QPolygonF(), QPen(QColor(100, 200, 255), 2));
  m_warpQuad->setZValue(99);

  updateWarpVisuals();
}

void OutputMappingCanvas::exitWarpMode()
{
  if(m_warpItemIndex < 0)
    return;

  for(int i = 0; i < int(m_warpHandles.size()); i++)
  {
    if(m_warpHandles[i])
    {
      m_scene.removeItem(m_warpHandles[i]);
      delete m_warpHandles[i];
      m_warpHandles[i] = nullptr;
    }
  }
  m_warpHandles.clear();
  if(m_warpQuad)
  {
    m_scene.removeItem(m_warpQuad);
    delete m_warpQuad;
    m_warpQuad = nullptr;
  }
  for(auto* line : m_warpGrid)
  {
    m_scene.removeItem(line);
    delete line;
  }
  m_warpGrid.clear();

  m_warpItemIndex = -1;
  m_warpDragging = false;
  m_warpDragHandle = -1;

  // Restore item flags based on their lock modes
  for(auto* gi : m_scene.items())
    if(auto* mi = dynamic_cast<OutputMappingItem*>(gi))
      mi->applyLockedState();
}

void OutputMappingCanvas::autoBlendOverlaps()
{
  std::vector<OutputMappingItem*> outputs;
  for(auto* gi : m_scene.items())
    if(auto* item = dynamic_cast<OutputMappingItem*>(gi)) outputs.push_back(item);
  for(auto* a : outputs)
  {
    a->blendLeft.width = a->blendRight.width = a->blendTop.width = a->blendBottom.width = 0.f;
    const auto r = a->mapRectToScene(a->rect());
    for(auto* b : outputs)
    {
      if(a == b) continue;
      const auto other = b->mapRectToScene(b->rect());
      const auto overlap = r.intersected(other);
      if(overlap.isEmpty()) continue;
      // Only opposing edges, never a fully contained output.
      if(overlap.height() >= .9 * qMin(r.height(), other.height()))
      {
        const float width = qMin(.5, overlap.width() / r.width());
        if(other.left() < r.left() && other.right() < r.right()) a->blendLeft.width = qMax(a->blendLeft.width, width);
        if(other.right() > r.right() && other.left() > r.left()) a->blendRight.width = qMax(a->blendRight.width, width);
      }
      if(overlap.width() >= .9 * qMin(r.width(), other.width()))
      {
        const float width = qMin(.5, overlap.height() / r.height());
        if(other.top() < r.top() && other.bottom() < r.bottom()) a->blendTop.width = qMax(a->blendTop.width, width);
        if(other.bottom() > r.bottom() && other.top() > r.top()) a->blendBottom.width = qMax(a->blendBottom.width, width);
      }
    }
    a->update();
  }
  if(onWarpChanged) onWarpChanged();
}

void OutputMappingCanvas::setMeshResolution(int columns, int rows)
{
  int index = m_warpItemIndex;
  if(index < 0)
    for(auto* selected : m_scene.selectedItems())
      if(auto* item = dynamic_cast<OutputMappingItem*>(selected))
        index = item->outputIndex();
  auto* item = findItemByIndex(index);
  if(!item) return;
  exitWarpMode();
  if(columns < 2 || rows < 2)
    item->meshWarp = {};
  else
    item->meshWarp.reset(columns, rows, item->cornerWarp);
  enterWarpMode(index);
  if(onWarpChanged) onWarpChanged();
}

void OutputMappingCanvas::resetWarp()
{
  // Reset warp for selected item (or warp-mode item)
  int idx = m_warpItemIndex >= 0 ? m_warpItemIndex : -1;
  if(idx < 0)
  {
    auto sel = m_scene.selectedItems();
    for(auto* gi : sel)
      if(auto* mi = dynamic_cast<OutputMappingItem*>(gi))
      {
        idx = mi->outputIndex();
        break;
      }
  }
  if(idx < 0)
    return;

  auto* item = findItemByIndex(idx);
  if(!item)
    return;

  item->cornerWarp = CornerWarp{};
  if(item->meshWarp.enabled())
    item->meshWarp.reset(item->meshWarp.columns, item->meshWarp.rows, item->cornerWarp);
  if(m_warpItemIndex >= 0)
    updateWarpVisuals();
  if(onWarpChanged)
    onWarpChanged();
}

void OutputMappingCanvas::updateWarpVisuals()
{
  auto* item = findItemByIndex(m_warpItemIndex);
  if(!item)
    return;

  auto r = item->mapRectToScene(item->rect());
  const auto& warp = item->cornerWarp;

  auto toScene = [&](const QPointF& uv) -> QPointF {
    return QPointF(r.left() + uv.x() * r.width(), r.top() + uv.y() * r.height());
  };

  if(item->meshWarp.enabled())
  {
    const auto& mesh = item->meshWarp;
    for(auto* line : m_warpGrid) delete line;
    m_warpGrid.clear();
    m_warpQuad->setPolygon(QPolygonF{});
    for(int y = 0; y < mesh.rows; ++y)
      for(int x = 0; x < mesh.columns; ++x)
      {
        const int i = y * mesh.columns + x;
        const auto p = toScene(mesh.points[i]);
        m_warpHandles[i]->setPos(p);
        const QPen pen(QColor(100, 200, 255, 180), 1);
        if(x + 1 < mesh.columns)
          m_warpGrid.push_back(m_scene.addLine(QLineF(p, toScene(mesh.points[i + 1])), pen));
        if(y + 1 < mesh.rows)
          m_warpGrid.push_back(m_scene.addLine(QLineF(p, toScene(mesh.points[i + mesh.columns])), pen));
      }
    return;
  }

  QPointF corners[4]
      = {toScene(warp.topLeft), toScene(warp.topRight), toScene(warp.bottomLeft),
         toScene(warp.bottomRight)};

  for(int i = 0; i < 4; i++)
    m_warpHandles[i]->setPos(corners[i]);

  // Quad outline: TL → TR → BR → BL → TL
  QPolygonF quad;
  quad << corners[0] << corners[1] << corners[3] << corners[2] << corners[0];
  m_warpQuad->setPolygon(quad);

  // Grid lines (4x4 bilinear subdivision)
  for(auto* line : m_warpGrid)
  {
    m_scene.removeItem(line);
    delete line;
  }
  m_warpGrid.clear();

  constexpr int gridN = 4;
  QPen gridPen(QColor(100, 200, 255, 80), 1);

  auto bilinear = [&](double u, double v) -> QPointF {
    return (1 - u) * (1 - v) * corners[0] + u * (1 - v) * corners[1]
           + (1 - u) * v * corners[2] + u * v * corners[3];
  };

  for(int row = 1; row < gridN; row++)
  {
    double v = (double)row / gridN;
    for(int col = 0; col < gridN; col++)
    {
      double u0 = (double)col / gridN;
      double u1 = (double)(col + 1) / gridN;
      m_warpGrid.push_back(
          m_scene.addLine(QLineF(bilinear(u0, v), bilinear(u1, v)), gridPen));
    }
  }
  for(int col = 1; col < gridN; col++)
  {
    double u = (double)col / gridN;
    for(int row = 0; row < gridN; row++)
    {
      double v0 = (double)row / gridN;
      double v1 = (double)(row + 1) / gridN;
      m_warpGrid.push_back(
          m_scene.addLine(QLineF(bilinear(u, v0), bilinear(u, v1)), gridPen));
    }
  }
}

void OutputMappingCanvas::mouseDoubleClickEvent(QMouseEvent* event)
{
  auto scenePos = mapToScene(event->pos());

  if(m_warpItemIndex >= 0)
  {
    // Don't exit if double-clicking on a handle
    for(int i = 0; i < int(m_warpHandles.size()); i++)
      if(m_warpHandles[i] && QLineF(scenePos, m_warpHandles[i]->pos()).length() < 10.0)
      {
        event->accept();
        return;
      }

    // Double-click on a different item → switch; same or empty → exit
    OutputMappingItem* hitItem = nullptr;
    for(auto* gi : m_scene.items(scenePos))
      if(auto* mi = dynamic_cast<OutputMappingItem*>(gi))
      {
        hitItem = mi;
        break;
      }

    if(hitItem && hitItem->outputIndex() != m_warpItemIndex)
      enterWarpMode(hitItem->outputIndex());
    else
      exitWarpMode();

    event->accept();
    return;
  }

  // Normal mode: double-click on item → enter warp mode
  for(auto* gi : m_scene.items(scenePos))
  {
    if(auto* mi = dynamic_cast<OutputMappingItem*>(gi))
    {
      enterWarpMode(mi->outputIndex());
      event->accept();
      return;
    }
  }
  QGraphicsView::mouseDoubleClickEvent(event);
}

void OutputMappingCanvas::mousePressEvent(QMouseEvent* event)
{
  if(m_warpItemIndex >= 0)
  {
    auto scenePos = mapToScene(event->pos());
    m_warpDragHandle = -1;
    double closest = 10.;
    for(int i = 0; i < int(m_warpHandles.size()); i++)
    {
      const double distance = QLineF(scenePos, m_warpHandles[i]->pos()).length();
      if(distance < closest)
      { closest = distance; m_warpDragHandle = i; }
    }
    m_warpDragging = m_warpDragHandle >= 0;
    if(m_warpDragging)
    {
      m_warpHandleAnchor = m_warpHandles[m_warpDragHandle]->pos();
      m_warpMouseAnchor = scenePos;
    }
    event->accept();
    return;
  }
  QGraphicsView::mousePressEvent(event);
}

void OutputMappingCanvas::mouseMoveEvent(QMouseEvent* event)
{
  if(m_warpItemIndex >= 0)
  {
    if(m_warpDragging && m_warpDragHandle >= 0)
    {
      auto* item = findItemByIndex(m_warpItemIndex);
      if(!item)
        return;

      const double scale = (event->modifiers() & Qt::ControlModifier) ? 0.1 : 1.0;
      auto r = item->mapRectToScene(item->rect());

      auto scenePos = mapToScene(event->pos());
      auto rawDelta = scenePos - m_warpMouseAnchor;
      auto targetPos = m_warpHandleAnchor + rawDelta * scale;

      // Convert to UV relative to item rect
      double u = (targetPos.x() - r.left()) / r.width();
      double v = (targetPos.y() - r.top()) / r.height();

      QPointF* corners[4]
          = {&item->cornerWarp.topLeft, &item->cornerWarp.topRight,
             &item->cornerWarp.bottomLeft, &item->cornerWarp.bottomRight};
      if(item->meshWarp.enabled())
      {
        const auto previous = item->meshWarp.points[m_warpDragHandle];
        item->meshWarp.points[m_warpDragHandle] = QPointF(u, v);
        if(!item->meshWarp.hasValidCells())
          item->meshWarp.points[m_warpDragHandle] = previous;
      }
      else
        *corners[m_warpDragHandle] = QPointF(u, v);

      updateWarpVisuals();

      if(onWarpChanged)
        onWarpChanged();
    }
    event->accept();
    return;
  }
  QGraphicsView::mouseMoveEvent(event);
}

void OutputMappingCanvas::mouseReleaseEvent(QMouseEvent* event)
{
  if(m_warpItemIndex >= 0)
  {
    m_warpDragging = false;
    m_warpDragHandle = -1;
    event->accept();
    return;
  }
  QGraphicsView::mouseReleaseEvent(event);
}

void OutputMappingCanvas::keyPressEvent(QKeyEvent* event)
{
  if(m_warpItemIndex >= 0 && event->key() == Qt::Key_Escape)
  {
    exitWarpMode();
    event->accept();
    return;
  }
  QGraphicsView::keyPressEvent(event);
}

}
