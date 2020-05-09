#include <QObject>
#include <QTimer>
#include <QTcpSocket>
#include <QPushButton>
#include <QMainWindow>

struct Packet {
  quint64 time;
  QByteArray data;

  Packet(quint64 time, QByteArray data);
};

class Replay : public QObject {
  Q_OBJECT

  std::vector<Packet> packets_;

  quint64 playback_index_;

  quint64 playback_time_ms_;

  qint32 playback_speed_;

  QTimer* timer_;

 public:
  Replay(QObject* parent, std::vector<Packet> packets);
  quint64 currentIndex();
  QByteArray packetData(quint64 index);

 public slots:
  void loop();
  void pause();
  void start(int playback_speed);

 signals:
  void packet(quint64 index);
};

class ReplayConnection : public QObject {
  Q_OBJECT

  Replay* replay_;
  QTcpSocket* socket_;

  quint64 packets_sent_;

  bool is_active_;

 public:
  ReplayConnection(QObject* parent, Replay* replay, QTcpSocket* socket);
  void handleRead();
  void sendPackets(quint64 index);
};

class ReplayWindow : public QMainWindow {
  Q_OBJECT

  Replay* replay_;

  QPushButton* load_button_;

  QPushButton* pause_button_;
  QPushButton* play_button_;
  QPushButton* ff2_button_;
  QPushButton* ff4_button_;
  QPushButton* ff8_button_;

 public:
  ReplayWindow(QWidget* parent);
 
 public slots:
  void loadFile(QString file);

 signals:
  void packet(quint64 index);
};
