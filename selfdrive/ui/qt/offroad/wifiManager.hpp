#pragma once

#include <QWidget>
#include <QtDBus>

enum class SecurityType {
  OPEN,
  WPA,
  UNSUPPORTED
};
enum class ConnectedType{
  DISCONNECTED,
  CONNECTING,
  CONNECTED
};

typedef QMap<QString, QMap<QString, QVariant>> Connection;

struct Network {
  QString path;
  QByteArray ssid;
  unsigned int strength;
  ConnectedType connected;
  SecurityType security_type;
};

class WifiManager : public QWidget {
  Q_OBJECT
public:
  explicit WifiManager();
  unsigned int adapter_state;//0 disconnected, 1 connecting, 2 connected

  bool has_adapter;
  void request_scan();
  QVector<Network> seen_networks;

  void refreshNetworks();
  void connect(Network ssid);
  void connect(Network ssid, QString password);
  void connect(Network ssid, QString username, QString password);

private:
  QVector<QByteArray> seen_ssids;
  QString adapter;//Path to network manager wifi-device
  QDBusConnection bus = QDBusConnection::systemBus();
  unsigned int raw_adapter_state;
  QString last_network;
  
  QString get_adapter();
  QList<Network> get_networks();
  void connect(QByteArray ssid, QString username, QString password, SecurityType security_type);
  QString get_active_ap();
  void deactivate_connections(QString ssid);
  void clear_connections(QString ssid);
  QVector<QDBusObjectPath> get_active_connections();
  uint get_wifi_device_state();
  QByteArray get_property(QString network_path, QString property);
  unsigned int get_ap_strength(QString network_path);
  SecurityType getSecurityType(QString ssid);

private slots:
  void change(unsigned int a, unsigned int b, unsigned int c);
signals:
  void wrongPassword(QString ssid);
};
