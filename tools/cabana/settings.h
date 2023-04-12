#pragma once

#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QGroupBox>
#include <QLineEdit>
#include <QSpinBox>

#define LIGHT_THEME 1
#define DARK_THEME 2

class Settings : public QObject {
  Q_OBJECT

public:
  Settings();
  void save();
  void load();

  int fps = 10;
  int max_cached_minutes = 30;
  int chart_height = 200;
  int chart_column_count = 1;
  int chart_range = 3 * 60; // 3 minutes
  int chart_series_type = 0;
  int theme = 0;
  int sparkline_range = 15; // 15 seconds
  bool log_livestream = true;
  QString log_path;
  QString last_dir;
  QString last_route_dir;
  QByteArray geometry;
  QByteArray video_splitter_state;
  QByteArray window_state;
  QStringList recent_files;
  QByteArray message_header_state;

signals:
  void changed();
};

class SettingsDlg : public QDialog {
  Q_OBJECT

public:
  SettingsDlg(QWidget *parent);
  void save();
  QSpinBox *fps;
  QSpinBox *cached_minutes;
  QSpinBox *chart_height;
  QComboBox *chart_series_type;
  QComboBox *theme;
  QGroupBox *log_livestream;
  QLineEdit *log_path;
};

extern Settings settings;
