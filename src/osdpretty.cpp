/*
 * Strawberry Music Player
 * This file was part of Clementine.
 * Copyright 2010, David Sansome <me@davidsansome.com>
 * Copyright 2019-2021, Jonas Kvinge <jonas@jkvinge.net>
 *
 * Strawberry is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Strawberry is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Strawberry.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <QtGlobal>
#include <QApplication>
#include <QGuiApplication>
#include <QWindow>
#include <QScreen>
#include <QWidget>
#include <QList>
#include <QVariant>
#include <QString>
#include <QImage>
#include <QPixmap>
#include <QBitmap>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QColor>
#include <QBrush>
#include <QCursor>
#include <QPen>
#include <QRect>
#include <QPoint>
#include <QFont>
#include <QTimer>
#include <QTimeLine>
#include <QTransform>
#include <QLayout>
#include <QBoxLayout>
#include <QLinearGradient>
#include <QSettings>
#include <QFlags>
#include <QtEvents>
#if defined(HAVE_X11) && defined(HAVE_QPA_QPLATFORMNATIVEINTERFACE_H)
#  include <qpa/qplatformnativeinterface.h>
#endif
#include <QDebug>

#include "osdpretty.h"
#include "ui_osdpretty.h"

#ifdef Q_OS_WIN
# include <windows.h>
# include <dwmapi.h>
#endif

const char *OSDPretty::kSettingsGroup = "OSDPretty";

const int OSDPretty::kDropShadowSize = 13;
const int OSDPretty::kBorderRadius = 10;
const int OSDPretty::kMaxIconSize = 100;

const int OSDPretty::kSnapProximity = 20;

const QRgb OSDPretty::kPresetBlue = qRgb(102, 150, 227);
const QRgb OSDPretty::kPresetRed = qRgb(202, 22, 16);

#ifdef Q_OS_WIN

namespace {

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
HRGN qt_RectToHRGN(const QRect &rc);
HRGN qt_RectToHRGN(const QRect &rc) {
  return CreateRectRgn(rc.left(), rc.top(), rc.right() + 1, rc.bottom() + 1);
}
#endif

HRGN toHRGN(const QRegion &region);
HRGN toHRGN(const QRegion &region) {

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return region.toHRGN();
#else

  const int rect_count = region.rectCount();
  if (rect_count == 0) {
    return nullptr;
  }

  HRGN resultRgn = nullptr;
  QRegion::const_iterator rects = region.begin();
  resultRgn = qt_RectToHRGN(rects[0]);
  for (int i = 1 ; i < rect_count ; ++i) {
    HRGN tmpRgn = qt_RectToHRGN(rects[i]);
    const int res = CombineRgn(resultRgn, resultRgn, tmpRgn, RGN_OR);
    if (res == ERROR) qWarning("Error combining HRGNs.");
    DeleteObject(tmpRgn);
  }

  return resultRgn;

#endif  // Qt 6

}

void enableBlurBehindWindow(QWindow *window, const QRegion &region) {

  DWM_BLURBEHIND dwmbb = {0, 0, nullptr, 0};
  dwmbb.dwFlags = DWM_BB_ENABLE;
  dwmbb.fEnable = TRUE;
  HRGN rgn = nullptr;
  if (!region.isNull()) {
    rgn = toHRGN(region);
    if (rgn) {
      dwmbb.hRgnBlur = rgn;
      dwmbb.dwFlags |= DWM_BB_BLURREGION;
    }
  }
  DwmEnableBlurBehindWindow(reinterpret_cast<HWND>(window->winId()), &dwmbb);
  if (rgn) {
    DeleteObject(rgn);
  }
}

}

#endif  // Q_OS_WIN

OSDPretty::OSDPretty(Mode mode, QWidget *parent)
    : QWidget(parent),
      ui_(new Ui_OSDPretty),
      mode_(mode),
      background_color_(kPresetBlue),
      background_opacity_(0.85),
      popup_screen_(nullptr),
      font_(QFont()),
      disable_duration_(false),
      timeout_(new QTimer(this)),
      fading_enabled_(false),
      fader_(new QTimeLine(300, this)),
      toggle_mode_(false) {

  Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint;

  setWindowFlags(flags);
  setAttribute(Qt::WA_TranslucentBackground, true);
  setAttribute(Qt::WA_X11NetWmWindowTypeNotification, true);
  setAttribute(Qt::WA_ShowWithoutActivating, true);
  ui_->setupUi(this);

#ifdef Q_OS_WIN
  // Don't show the window in the taskbar.  Qt::ToolTip does this too, but it adds an extra ugly shadow.
  int ex_style = GetWindowLong((HWND) winId(), GWL_EXSTYLE);
  ex_style |= WS_EX_NOACTIVATE;
  SetWindowLong((HWND) winId(), GWL_EXSTYLE, ex_style);
#endif

  // Mode settings
  switch (mode_) {
    case Mode_Popup:
      setCursor(QCursor(Qt::ArrowCursor));
      break;

    case Mode_Draggable:
      setCursor(QCursor(Qt::OpenHandCursor));
      break;
  }

  // Timeout
  timeout_->setSingleShot(true);
  timeout_->setInterval(5000);
  QObject::connect(timeout_, &QTimer::timeout, this, &OSDPretty::hide);

  ui_->icon->setMaximumSize(kMaxIconSize, kMaxIconSize);

  // Fader
  QObject::connect(fader_, &QTimeLine::valueChanged, this, &OSDPretty::FaderValueChanged);
  QObject::connect(fader_, &QTimeLine::finished, this, &OSDPretty::FaderFinished);

  // Load the show edges and corners
  QImage shadow_edge(":/osd_shadow_edge.png");
  QImage shadow_corner(":/osd_shadow_corner.png");
  for (int i = 0; i < 4; ++i) {
    QTransform rotation = QTransform().rotate(90 * i);
    shadow_edge_[i] = QPixmap::fromImage(shadow_edge.transformed(rotation));
    shadow_corner_[i] = QPixmap::fromImage(shadow_corner.transformed(rotation));
  }
  background_ = QPixmap(":/osd_background.png");

  // Set the margins to allow for the drop shadow
  QBoxLayout *l = qobject_cast<QBoxLayout*>(layout());
  QMargins margin = l->contentsMargins();
  margin.setTop(margin.top() + kDropShadowSize);
  margin.setBottom(margin.bottom() + kDropShadowSize);
  margin.setLeft(margin.left() + kDropShadowSize);
  margin.setRight(margin.right() + kDropShadowSize);
  l->setContentsMargins(margin);

  QObject::connect(qApp, &QApplication::screenAdded, this, &OSDPretty::ScreenAdded);
  QObject::connect(qApp, &QApplication::screenRemoved, this, &OSDPretty::ScreenRemoved);

}

OSDPretty::~OSDPretty() {
  delete ui_;
}

void OSDPretty::showEvent(QShowEvent *e) {

  screens_.clear();
  for(QScreen *screen : qApp->screens()) {
    screens_.insert(screen->name(), screen);
  }

  // Get current screen resolution
  QScreen *screen = current_screen();
  if (screen) {
    QRect resolution = screen->availableGeometry();
    // Leave 200 px for icon
    ui_->summary->setMaximumWidth(resolution.width() - 200);
    ui_->message->setMaximumWidth(resolution.width() - 200);
    // Set maximum size for the OSD, a little margin here too
    setMaximumSize(resolution.width() - 100, resolution.height() - 100);
  }

  setWindowOpacity(fading_enabled_ ? 0.0 : 1.0);

  QWidget::showEvent(e);

  Load();
  Reposition();

  if (fading_enabled_) {
    fader_->setDirection(QTimeLine::Forward);
    fader_->start();  // Timeout will be started in FaderFinished
  }
  else if (mode_ == Mode_Popup) {
    if (!disable_duration())
      timeout_->start();
    // Ensures it is above when showing the preview
    raise();
  }

}

void OSDPretty::ScreenAdded(QScreen *screen) {

  screens_.insert(screen->name(), screen);

}

void OSDPretty::ScreenRemoved(QScreen *screen) {

  if (screens_.contains(screen->name())) screens_.remove(screen->name());
  if (screen == popup_screen_) popup_screen_ = current_screen();

}

bool OSDPretty::IsTransparencyAvailable() {

#if defined(HAVE_X11) && defined(HAVE_QPA_QPLATFORMNATIVEINTERFACE_H)
  if (qApp) {
    QPlatformNativeInterface *native = qApp->platformNativeInterface();
    QScreen *screen = popup_screen_ == nullptr ?  QGuiApplication::primaryScreen() : popup_screen_;
    if (native && screen) {
      return native->nativeResourceForScreen(QByteArray("compositingEnabled"), screen);
    }
  }
#endif

  return true;

}

void OSDPretty::Load() {

  QSettings s;
  s.beginGroup(kSettingsGroup);
  foreground_color_ = QColor(s.value("foreground_color", 0).toInt());
  background_color_ = QColor(s.value("background_color", kPresetBlue).toInt());
  background_opacity_ = s.value("background_opacity", 0.85).toFloat();
  font_.fromString(s.value("font", "Verdana,9,-1,5,50,0,0,0,0,0").toString());
  disable_duration_ = s.value("disable_duration", false).toBool();
#ifdef Q_OS_WIN
  fading_enabled_ = s.value("fading", true).toBool();
#else
  fading_enabled_ = s.value("fading", false).toBool();
#endif

  if (s.contains("popup_screen")) {
    popup_screen_name_ = s.value("popup_screen").toString();
    if (screens_.contains(popup_screen_name_)) {
      popup_screen_ = screens_[popup_screen_name_];
    }
    else {
      popup_screen_ = current_screen();
      if (current_screen()) popup_screen_name_ = current_screen()->name();
      else popup_screen_name_.clear();
    }
  }
  else {
    popup_screen_ = current_screen();
    if (current_screen()) popup_screen_name_ = current_screen()->name();
  }

  if (s.contains("popup_pos")) {
    popup_pos_ = s.value("popup_pos").toPoint();
  }
  else {
    if (popup_screen_) {
      QRect geometry = popup_screen_->availableGeometry();
      popup_pos_.setX(geometry.width() - width());
      popup_pos_.setY(0);
    }
    else {
      popup_pos_.setX(0);
      popup_pos_.setY(0);
    }
  }

  set_font(font());
  set_foreground_color(foreground_color());

  s.endGroup();

}

void OSDPretty::ReloadSettings() {
  Load();
  if (isVisible()) update();
}

QRect OSDPretty::BoxBorder() const {
  return rect().adjusted(kDropShadowSize, kDropShadowSize, -kDropShadowSize, -kDropShadowSize);
}

void OSDPretty::paintEvent(QPaintEvent*) {

  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  QRect box(BoxBorder());

  // Shadow corners
  const int kShadowCornerSize = kDropShadowSize + kBorderRadius;
  p.drawPixmap(0, 0, shadow_corner_[0]);
  p.drawPixmap(width() - kShadowCornerSize, 0, shadow_corner_[1]);
  p.drawPixmap(width() - kShadowCornerSize, height() - kShadowCornerSize, shadow_corner_[2]);
  p.drawPixmap(0, height() - kShadowCornerSize, shadow_corner_[3]);

  // Shadow edges
  p.drawTiledPixmap(kShadowCornerSize, 0, width() - kShadowCornerSize*2, kDropShadowSize, shadow_edge_[0]);
  p.drawTiledPixmap(width() - kDropShadowSize, kShadowCornerSize, kDropShadowSize, height() - kShadowCornerSize*2, shadow_edge_[1]);
  p.drawTiledPixmap(kShadowCornerSize, height() - kDropShadowSize, width() - kShadowCornerSize*2, kDropShadowSize, shadow_edge_[2]);
  p.drawTiledPixmap(0, kShadowCornerSize, kDropShadowSize, height() - kShadowCornerSize*2, shadow_edge_[3]);

  // Box background
  p.setBrush(background_color_);
  p.setPen(QPen());
  p.setOpacity(background_opacity_);
  p.drawRoundedRect(box, kBorderRadius, kBorderRadius);

  // Background pattern
  QPainterPath background_path;
  background_path.addRoundedRect(box, kBorderRadius, kBorderRadius);
  p.setClipPath(background_path);
  p.setOpacity(1.0);
  p.drawPixmap(box.right() - background_.width(), box.bottom() - background_.height(), background_);
  p.setClipping(false);

  // Gradient overlay
  QLinearGradient gradient(0, 0, 0, height());
  gradient.setColorAt(0, QColor(255, 255, 255, 130));
  gradient.setColorAt(1, QColor(255, 255, 255, 50));
  p.setBrush(gradient);
  p.drawRoundedRect(box, kBorderRadius, kBorderRadius);

  // Box border
  p.setBrush(QBrush());
  p.setPen(QPen(background_color_.darker(150), 2));
  p.drawRoundedRect(box, kBorderRadius, kBorderRadius);

}

void OSDPretty::SetMessage(const QString &summary, const QString &message, const QImage &image) {

  if (!image.isNull()) {
    QImage scaled_image = image.scaled(kMaxIconSize, kMaxIconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    ui_->icon->setPixmap(QPixmap::fromImage(scaled_image));
    ui_->icon->show();
  }
  else {
    ui_->icon->hide();
  }

  ui_->summary->setText(summary);
  ui_->message->setText(message);

  if (isVisible()) Reposition();

}

// Set the desired message and then show the OSD
void OSDPretty::ShowMessage(const QString &summary, const QString &message, const QImage &image) {

  SetMessage(summary, message, image);

  if (isVisible() && mode_ == Mode_Popup) {
    // The OSD is already visible, toggle or restart the timer
    if (toggle_mode()) {
      set_toggle_mode(false);
      // If timeout is disabled, timer hadn't been started
      if (!disable_duration())
        timeout_->stop();
      hide();
    }
    else {
      if (!disable_duration())
        timeout_->start(); // Restart the timer
    }
  }
  else {
    if (toggle_mode())
      set_toggle_mode(false);
    // The OSD is not visible, show it
    show();
  }

}

void OSDPretty::setVisible(bool visible) {

  if (!visible && fading_enabled_ && fader_->direction() == QTimeLine::Forward) {
    fader_->setDirection(QTimeLine::Backward);
    fader_->start();
  }
  else {
    QWidget::setVisible(visible);
  }

}

void OSDPretty::FaderFinished() {

  if (fader_->direction() == QTimeLine::Backward)
    hide();
  else if (mode_ == Mode_Popup && !disable_duration())
    timeout_->start();

}

void OSDPretty::FaderValueChanged(const qreal value) {
  setWindowOpacity(value);
}

void OSDPretty::Reposition() {

  // Make the OSD the proper size
  layout()->activate();
  resize(sizeHint());

  // Work out where to place the OSD.  -1 for x or y means "on the right or bottom edge".
  if (popup_screen_) {

    QRect geometry = popup_screen_->availableGeometry();

    int x = popup_pos_.x() < 0 ? geometry.right() - width() : geometry.left() + popup_pos_.x();
    int y = popup_pos_.y() < 0 ? geometry.bottom() - height() : geometry.top() + popup_pos_.y();

#ifndef Q_OS_WIN
    x = qBound(0, x, geometry.right() - width());
    y = qBound(0, y, geometry.bottom() - height());
#endif
    move(x, y);
  }

  // Create a mask for the actual area of the OSD
  QBitmap mask(size());
  mask.clear();

  QPainter p(&mask);
  p.setBrush(Qt::color1);
  p.drawRoundedRect(BoxBorder().adjusted(-1, -1, 0, 0), kBorderRadius, kBorderRadius);
  p.end();

  // If there's no compositing window manager running then we have to set an XShape mask.
  if (IsTransparencyAvailable())
    clearMask();
  else {
    setMask(mask);
  }

  // On windows, enable blurbehind on the masked area
#ifdef Q_OS_WIN
  enableBlurBehindWindow(this->windowHandle(), QRegion(mask));
#endif

}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void OSDPretty::enterEvent(QEnterEvent*) {
#else
void OSDPretty::enterEvent(QEvent*) {
#endif
  if (mode_ == Mode_Popup)
    setWindowOpacity(0.25);
}

void OSDPretty::leaveEvent(QEvent*) {
  setWindowOpacity(1.0);
}

void OSDPretty::mousePressEvent(QMouseEvent *e) {

  if (mode_ == Mode_Popup)
    hide();
  else {
    original_window_pos_ = pos();
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    drag_start_pos_ = e->globalPosition().toPoint();
#else
    drag_start_pos_ = e->globalPos();
#endif
  }

}

void OSDPretty::mouseMoveEvent(QMouseEvent *e) {

  if (mode_ == Mode_Draggable) {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    QPoint delta = e->globalPosition().toPoint() - drag_start_pos_;
#else
    QPoint delta = e->globalPos() - drag_start_pos_;
#endif
    QPoint new_pos = original_window_pos_ + delta;

    // Keep it to the bounds of the desktop
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    QScreen *screen = current_screen(e->globalPosition().toPoint());
#else
    QScreen *screen = current_screen(e->globalPos());
#endif
    if (!screen) return;

    QRect geometry = screen->availableGeometry();

    new_pos.setX(qBound(geometry.left(), new_pos.x(), geometry.right() - width()));
    new_pos.setY(qBound(geometry.top(), new_pos.y(), geometry.bottom() - height()));

    // Snap to center
    int snap_x = geometry.center().x() - width() / 2;
    if (new_pos.x() > snap_x - kSnapProximity && new_pos.x() < snap_x + kSnapProximity) {
      new_pos.setX(snap_x);
    }

    move(new_pos);

    popup_screen_ = screen;
    popup_screen_name_ = screen->name();
  }

}

void OSDPretty::mouseReleaseEvent(QMouseEvent *) {

  if (current_screen() && mode_ == Mode_Draggable) {
    popup_screen_ = current_screen();
    popup_screen_name_ = current_screen()->name();
    popup_pos_ = current_pos();
    emit PositionChanged();
  }

}

QScreen *OSDPretty::current_screen(const QPoint &pos) const {

  QScreen *screen(nullptr);
#if (QT_VERSION >= QT_VERSION_CHECK(5, 10, 0))
  screen = QGuiApplication::screenAt(pos);
#else
  Q_UNUSED(pos)
  if (window() && window()->windowHandle()) screen = window()->windowHandle()->screen();
#endif
  if (!screen) screen = QGuiApplication::primaryScreen();

  return screen;

}

QScreen *OSDPretty::current_screen() const { return current_screen(pos()); }

QPoint OSDPretty::current_pos() const {

  if (current_screen()) {
    QRect geometry = current_screen()->availableGeometry();

    int x = pos().x() >= geometry.right() - width() ? -1 : pos().x() - geometry.left();
    int y = pos().y() >= geometry.bottom() - height() ? -1 : pos().y() - geometry.top();

    return QPoint(x, y);
  }

  return QPoint(0, 0);

}

void OSDPretty::set_background_color(const QRgb color) {
  background_color_ = color;
  if (isVisible()) update();
}

void OSDPretty::set_background_opacity(const qreal opacity) {
  background_opacity_ = opacity;
  if (isVisible()) update();
}

void OSDPretty::set_foreground_color(const QRgb color) {

  foreground_color_ = QColor(color);

  QPalette p;
  p.setColor(QPalette::WindowText, foreground_color_);

  ui_->summary->setPalette(p);
  ui_->message->setPalette(p);

}

void OSDPretty::set_popup_duration(const int msec) {
  timeout_->setInterval(msec);
}

void OSDPretty::set_font(const QFont font) {

  font_ = font;

  // Update the UI
  ui_->summary->setFont(font);
  ui_->message->setFont(font);
  // Now adjust OSD size so everything fits
  ui_->verticalLayout->activate();
  resize(sizeHint());
  // Update the position after font change
  Reposition();

}
