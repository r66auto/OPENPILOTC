#include "selfdrive/ui/qt/onroad.h"

#include <iostream>
#include <QDebug>

#include "selfdrive/common/swaglog.h"
#include "selfdrive/common/timing.h"
#include "selfdrive/ui/paint.h"
#include "selfdrive/ui/qt/util.h"

#ifdef ENABLE_MAPS
#include "selfdrive/ui/qt/maps/map.h"
#endif

static inline VisionStreamType getStreamType() {
  return Hardware::TICI() && Params().getBool("EnableWideCamera") ? VISION_STREAM_RGB_WIDE : VISION_STREAM_RGB_BACK;
}

OnroadWindow::OnroadWindow(QWidget *parent) : QWidget(parent) {
  main_layout = new QStackedLayout(this);
  main_layout->setStackingMode(QStackedLayout::StackAll);

  nvg = new NvgWindow(getStreamType(), this);

  QWidget * split_wrapper = new QWidget;
  split = new QHBoxLayout(split_wrapper);
  split->setContentsMargins(0, 0, 0, 0);
  split->setSpacing(0);
  split->addWidget(nvg);

  main_layout->addWidget(split_wrapper);

  alerts = new OnroadAlerts(this);
  alerts->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  QObject::connect(this, &OnroadWindow::update, alerts, &OnroadAlerts::updateState);
  QObject::connect(this, &OnroadWindow::offroadTransitionSignal, alerts, &OnroadAlerts::offroadTransition);
  QObject::connect(this, &OnroadWindow::offroadTransitionSignal, this, &OnroadWindow::offroadTransition);
  main_layout->addWidget(alerts);

  scene = new OnroadScene(this);
  QObject::connect(this, &OnroadWindow::update, scene, &OnroadScene::updateState);
  QObject::connect(this, &OnroadWindow::offroadTransitionSignal, scene, &OnroadScene::offroadTransition);
  main_layout->addWidget(scene);

  // setup stacking order
  alerts->raise();
  scene->raise();
  
  

  setAttribute(Qt::WA_OpaquePaintEvent);
}


void OnroadWindow::offroadTransition(bool offroad) {
#ifdef ENABLE_MAPS
  if (!offroad) {
    QString token = QString::fromStdString(Params().get("MapboxToken"));
    if (map == nullptr && !token.isEmpty()) {
      QMapboxGLSettings settings;
      if (!Hardware::PC()) {
        settings.setCacheDatabasePath("/data/mbgl-cache.db");
      }
      settings.setCacheDatabaseMaximumSize(20 * 1024 * 1024);
      settings.setAccessToken(token.trimmed());

      MapWindow * m = new MapWindow(settings);
      QObject::connect(this, &OnroadWindow::offroadTransitionSignal, m, &MapWindow::offroadTransition);
      split->addWidget(m);

      map = m;
    }

  }
#endif
}

// ***** onroad widgets *****

OnroadAlerts::OnroadAlerts(QWidget *parent) : QWidget(parent) {
  std::tuple<AudibleAlert, QString, bool> sound_list[] = {
    {AudibleAlert::CHIME_DISENGAGE, "../assets/sounds/disengaged.wav", false},
    {AudibleAlert::CHIME_ENGAGE, "../assets/sounds/engaged.wav", false},
    {AudibleAlert::CHIME_WARNING1, "../assets/sounds/warning_1.wav", false},
    {AudibleAlert::CHIME_WARNING2, "../assets/sounds/warning_2.wav", false},
    {AudibleAlert::CHIME_WARNING2_REPEAT, "../assets/sounds/warning_2.wav", true},
    {AudibleAlert::CHIME_WARNING_REPEAT, "../assets/sounds/warning_repeat.wav", true},
    {AudibleAlert::CHIME_ERROR, "../assets/sounds/error.wav", false},
    {AudibleAlert::CHIME_PROMPT, "../assets/sounds/error.wav", false}};

  for (auto &[alert, fn, loops] : sound_list) {
    sounds[alert].first.setSource(QUrl::fromLocalFile(fn));
    sounds[alert].second = loops ? QSoundEffect::Infinite : 0;
  }
}

void OnroadAlerts::updateState(const UIState &s) {
  SubMaster &sm = *(s.sm);
  UIStatus status = s.status;
  if (sm.updated("carState")) {
    // scale volume with speed
    volume = util::map_val(sm["carState"].getCarState().getVEgo(), 0.f, 20.f,
                           Hardware::MIN_VOLUME, Hardware::MAX_VOLUME);
  }
  if (sm["deviceState"].getDeviceState().getStarted()) {
    if (sm.updated("controlsState")) {
      const cereal::ControlsState::Reader &cs = sm["controlsState"].getControlsState();
      updateAlert(QString::fromStdString(cs.getAlertText1()), QString::fromStdString(cs.getAlertText2()),
                  cs.getAlertBlinkingRate(), cs.getAlertType(), cs.getAlertSize(), cs.getAlertSound());
    } else if ((sm.frame - s.scene.started_frame) > 10 * UI_FREQ) {
      // Handle controls timeout
      if (sm.rcv_frame("controlsState") < s.scene.started_frame) {
        // car is started, but controlsState hasn't been seen at all
        updateAlert("openpilot Unavailable", "Waiting for controls to start", 0,
                    "controlsWaiting", cereal::ControlsState::AlertSize::MID, AudibleAlert::NONE);
      } else if ((sm.frame - sm.rcv_frame("controlsState")) > 5 * UI_FREQ) {
        // car is started, but controls is lagging or died
        updateAlert("TAKE CONTROL IMMEDIATELY", "Controls Unresponsive", 0,
                    "controlsUnresponsive", cereal::ControlsState::AlertSize::FULL, AudibleAlert::CHIME_WARNING_REPEAT);
      }
      status = STATUS_ALERT;
    }
  }


  // TODO: add blinking back if performant
  //float alpha = 0.375 * cos((millis_since_boot() / 1000) * 2 * M_PI * blinking_rate) + 0.625;
  bg = bg_colors[status];
  if (status != prev_status) {
    update();
    prev_status = status;
  }
}

void OnroadAlerts::offroadTransition(bool offroad) {
  updateAlert("", "", 0, "", cereal::ControlsState::AlertSize::NONE, AudibleAlert::NONE);
}

void OnroadAlerts::updateAlert(const QString &t1, const QString &t2, float blink_rate,
                               const std::string &type, cereal::ControlsState::AlertSize size, AudibleAlert sound) {
  if (alert_type.compare(type) == 0 && text1.compare(t1) == 0 && text2.compare(t2) == 0) {
    return;
  }

  stopSounds();
  if (sound != AudibleAlert::NONE) {
    playSound(sound);
  }

  text1 = t1;
  text2 = t2;
  alert_type = type;
  alert_size = size;
  blinking_rate = blink_rate;

  update();
}

void OnroadAlerts::playSound(AudibleAlert alert) {
  auto &[sound, loops] = sounds[alert];
  sound.setLoopCount(loops);
  sound.setVolume(volume);
  sound.play();
}

void OnroadAlerts::stopSounds() {
  for (auto &kv : sounds) {
    // Only stop repeating sounds
    auto &[sound, loops] = kv.second;
    if (sound.loopsRemaining() == QSoundEffect::Infinite) {
      sound.stop();
    }
  }
}

void OnroadAlerts::paintEvent(QPaintEvent *event) {
  // draw border
  QPainter p(this);
  p.setPen(QPen(bg, bdr_s));
  p.drawRect(rect());
  p.setPen(Qt::NoPen);

  if (alert_size == cereal::ControlsState::AlertSize::NONE) {
    return;
  }
  static std::map<cereal::ControlsState::AlertSize, const int> alert_sizes = {
    {cereal::ControlsState::AlertSize::SMALL, 271},
    {cereal::ControlsState::AlertSize::MID, 420},
    {cereal::ControlsState::AlertSize::FULL, height()},
  };
  int h = alert_sizes[alert_size];
  QRect r = QRect(0, height() - h, width(), h);

  // draw background + gradient
  p.setPen(Qt::NoPen);
  p.setCompositionMode(QPainter::CompositionMode_SourceOver);

  p.setBrush(QBrush(bg));
  p.drawRect(r);

  QLinearGradient g(0, r.y(), 0, r.bottom());
  g.setColorAt(0, QColor::fromRgbF(0, 0, 0, 0.05));
  g.setColorAt(1, QColor::fromRgbF(0, 0, 0, 0.35));

  p.setCompositionMode(QPainter::CompositionMode_DestinationOver);
  p.setBrush(QBrush(g));
  p.fillRect(r, g);
  p.setCompositionMode(QPainter::CompositionMode_SourceOver);

  // remove bottom border
  r = QRect(0, height() - h, width(), h - 30);

  // text
  const QPoint c = r.center();
  p.setPen(QColor(0xff, 0xff, 0xff));
  p.setRenderHint(QPainter::TextAntialiasing);
  if (alert_size == cereal::ControlsState::AlertSize::SMALL) {
    configFont(p, "Open Sans", 74, "SemiBold");
    p.drawText(r, Qt::AlignCenter, text1);
  } else if (alert_size == cereal::ControlsState::AlertSize::MID) {
    configFont(p, "Open Sans", 88, "Bold");
    p.drawText(QRect(0, c.y() - 125, width(), 150), Qt::AlignHCenter | Qt::AlignTop, text1);
    configFont(p, "Open Sans", 66, "Regular");
    p.drawText(QRect(0, c.y() + 21, width(), 90), Qt::AlignHCenter, text2);
  } else if (alert_size == cereal::ControlsState::AlertSize::FULL) {
    bool l = text1.length() > 15;
    configFont(p, "Open Sans", l ? 132 : 177, "Bold");
    p.drawText(QRect(0, r.y() + (l ? 240 : 270), width(), 600), Qt::AlignHCenter | Qt::TextWordWrap, text1);
    configFont(p, "Open Sans", 88, "Regular");
    p.drawText(QRect(0, r.height() - (l ? 361 : 420), width(), 300), Qt::AlignHCenter | Qt::TextWordWrap, text2);
  }
}

// NvgWindow

NvgWindow::NvgWindow(VisionStreamType type, QWidget *parent) : CameraViewWidget(type, parent) {}

void NvgWindow::initializeGL() {
  CameraViewWidget::initializeGL();
  ui_nvg_init(&QUIState::ui_state);
}

void NvgWindow::resizeGL(int w, int h) {
  CameraViewWidget::resizeGL(w, h);
  ui_resize(&QUIState::ui_state, w, h);
}

void NvgWindow::paintGL() {
  CameraViewWidget::paintGL();
  ui_draw(&QUIState::ui_state, width(), height());
}

void NvgWindow::showEvent(QShowEvent *event) {
  CameraViewWidget::showEvent(event);
  // Update vistion stream after possible wide camera toggle change
  setStreamType(getStreamType());
  ui_resize(&QUIState::ui_state, rect().width(), rect().height());
}


OnroadScene::OnroadScene(QWidget *parent) : QWidget(parent) {
  setAttribute(Qt::WA_TransparentForMouseEvents, true);
  engage_img = QPixmap("../assets/img_chffr_wheel.png").scaled(img_size, img_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  dm_img = QPixmap("../assets/img_driver_face.png").scaled(img_size, img_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void OnroadScene::offroadTransition(bool offroad) {
  metric = Params().getBool("IsMetric");
  connect(this, &OnroadScene::valueChanged, [=] { update(); });
}

void OnroadScene::updateState(const UIState &s) {
  SubMaster &sm = *(s.sm);

  auto v = sm["carState"].getCarState().getVEgo() * (metric ? 3.6 : 2.2369363);
  setProperty("speed", QString::number(int(v)));
  setProperty("speedUnit", metric ? "km/h" : "mph");

  auto cs = sm["controlsState"].getControlsState();
  const int SET_SPEED_NA = 255;
  auto vcruise = cs.getVCruise();
  if (vcruise != 0 && vcruise != SET_SPEED_NA) {
    auto max = vcruise * (metric ? 1 : 0.6225);
    setProperty("maxSpeed", QString::number((int)max));
  } else {
    setProperty("maxSpeed", "N/A");
  }

  setProperty("dmActive", sm["driverMonitoringState"].getDriverMonitoringState().getIsActiveMode());
  setProperty("hideDM", cs.getAlertSize() == cereal::ControlsState::AlertSize::NONE);
  setProperty("engageable", cs.getEngageable());
  setProperty("status", s.status);
}

void OnroadScene::drawIcon(QPainter &p, const QPoint &center, QPixmap &img, QBrush bg, float opacity) {
  p.setPen(Qt::NoPen);
  p.setBrush(bg);
  QRect rc = {center.x() - radius / 2, center.y() - radius / 2, radius, radius};
  p.drawEllipse(rc);
  p.setOpacity(opacity);

  p.drawPixmap(center.x() - img_size / 2, center.y() - img_size / 2, img);
}

void OnroadScene::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  // max speed
  QRect rc(bdr_s * 2, bdr_s * 1.5, 184, 202);
  rc.moveLeft(300);
  p.setPen(QPen(QColor(0xff, 0xff, 0xff, 100), 10));
  p.setBrush(QColor(0, 0, 0, 100));
  p.drawRoundedRect(rc, 20, 20);
  
  QRect rcText = rc;
  rcText.setBottom(120);
  p.setPen(QColor(0xff, 0xff, 0xff, 200));
  configFont(p, "Open Sans", 45, "Regular");
  p.drawText(rcText, Qt::AlignCenter,  "MAX");
  rcText = rc;
  rcText.setTop(120);
  p.setPen(QColor(0xff, 0xff, 0xff, 100));
  configFont(p, "Open Sans", 90, "SemiBold");
  p.drawText(rcText, Qt::AlignCenter, maxSpeed_);

  // current speed
  p.setPen(Qt::white);
  configFont(p, "Open Sans", 186, "Bold");
  p.drawText(rect().adjusted(0, 112, 0, 0), Qt::AlignHCenter | Qt::AlignTop, speed_);

  // engage-ability icon
  // if (engageable_) {
    drawIcon(p, {rect().right() - radius - bdr_s*2, radius  + int(bdr_s * 1.5)}, engage_img, bg_colors[status_], 1.0);
  // }
  
  // dm icon
  drawIcon(p, {radius + (bdr_s * 2), rect().bottom() - footer_h / 2}, dm_img, QColor(0, 0, 0, 70), dmActive_ ? 1.0: 0.2);
}
