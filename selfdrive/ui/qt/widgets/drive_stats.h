#pragma once

#include <QJsonDocument>
#include <QLabel>

#include "selfdrive/ui/qt/widgets/controls.h"

class DriveStats : public QFrame, public UI {
  Q_OBJECT

public:
  explicit DriveStats(QWidget* parent = 0);
  void translateUi() override;

private:
  void showEvent(QShowEvent *event) override;
  void updateStats();
  inline QString getDistanceUnit() const { return metric_ ? tr("KM") : tr("Miles"); }

  bool metric_;
  QJsonDocument stats_;
  struct StatsLabels {
    QLabel *routes, *distance, *distance_unit, *hours;
  } all_, week_;

private slots:
  void parseResponse(const QString &response, bool success);
};
