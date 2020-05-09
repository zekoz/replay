#include <QByteArray>
#include <QFile>
#include <QObject>
#include <QTimer>
#include <QTcpSocket>
#include <QTcpServer>
#include <QWidget>
#include <QApplication>
#include <QFileDialog>
#include <QPushButton>
#include <QMainWindow>
#include <QHBoxLayout>

#include "replayer.h"

Packet::Packet(quint64 time, QByteArray data) : time(time), data(data) {}

Replay::Replay(QObject* parent, std::vector<Packet> packets)
    : QObject(parent),
      packets_(std::move(packets)),
      playback_index_(0),
      playback_time_ms_(0),
      playback_speed_(0) {
  timer_ = new QTimer(this);
  timer_->setSingleShot(true);

  for (; playback_index_ < packets_.size(); playback_index_++) {
    if (packets_[playback_index_].time > 0)
      break;
  }

  connect(timer_, &QTimer::timeout, this, &Replay::loop);
}

quint64 Replay::currentIndex() {
  return playback_index_;
}

QByteArray Replay::packetData(quint64 index) {
  return packets_[index].data;
}

void Replay::pause() {
  timer_->stop();
  playback_time_ms_ += (timer_->interval() - timer_->remainingTime()) *
                       playback_speed_;
  playback_speed_ = 0;
}

void Replay::start(int playback_speed = 1) {
  bool start_loop = playback_speed_ != 0;

  playback_speed_ = playback_speed;

  if (start_loop) {
    loop();
  }
}

void Replay::loop() {
  if (!playback_speed_) {
    return;
  }

  long stop_before = playback_time_ms_ + timer_->interval() * playback_speed_ +
                     playback_speed_;

  for (; playback_index_ < packets_.size(); playback_index_++) {
    if (packets_[playback_index_].time > stop_before) {
      break;
    }
    playback_time_ms_ = packets_[playback_index_].time;
  }

  qInfo("now %d", playback_time_ms_);
	    
  if (playback_index_ > 0) {
    emit packet(playback_index_);
    qInfo("emit %d", playback_index_);
  }

  if (playback_index_ >= packets_.size()) {
    return;
  }

  quint64 next_time = packets_[playback_index_].time;
  quint64 real_wait = (next_time - playback_time_ms_) / playback_speed_;
  qInfo("wait %d until %d", real_wait, next_time);

  timer_->setInterval(real_wait);
  timer_->start();
}

std::vector<Packet> parse(QFile *input) {
  std::vector<Packet> ret;

  while (!input->atEnd()) {
    QByteArray buf = input->read(8);
    quint64 time_ms = *reinterpret_cast<quint64*>(buf.data());
    buf = input->read(4);
    int length = *reinterpret_cast<int*>(buf.data());
    buf = input->read(length);
    ret.push_back(Packet(time_ms, buf));
  }

  return ret;
}

ReplayConnection::ReplayConnection(QObject* parent, Replay* replay,
    QTcpSocket* socket)
    : QObject(parent),
      replay_(replay),
      socket_(socket),
      packets_sent_(0),
      is_active_(false) {}

void ReplayConnection::handleRead() {
  socket_->read(socket_->bytesAvailable());
  if (!is_active_) {
    is_active_ = true;
    connect(replay_, &Replay::packet, this, &ReplayConnection::sendPackets);
    sendPackets(replay_->currentIndex());
  }
}

void ReplayConnection::sendPackets(quint64 index) {
  for (; packets_sent_ < index; packets_sent_++) {
    QByteArray packet = replay_->packetData(packets_sent_);
    socket_->write(packet);
  }
}

ReplayWindow::ReplayWindow(QWidget* parent = nullptr)
    : QMainWindow(parent, Qt::WindowStaysOnTopHint) {
  load_button_ = new QPushButton("Load");
  connect(load_button_, &QPushButton::clicked, load_button_, [=]() {
    QFileDialog* dlg = new QFileDialog(this);
    connect(dlg, &QFileDialog::fileSelected, this, &ReplayWindow::loadFile);
    dlg->show();
  });

  pause_button_ = new QPushButton("||");
  connect(pause_button_, &QPushButton::clicked, this, [=]() {
    pause_button_->setEnabled(false);
    replay_->pause();
  });
  pause_button_->setEnabled(false);

  play_button_ = new QPushButton(">");
  connect(play_button_, &QPushButton::clicked, this, [=]() {
    pause_button_->setEnabled(true);
    replay_->start(1);
  });
  play_button_->setEnabled(false);

  ff2_button_ = new QPushButton("2x");
  connect(ff2_button_, &QPushButton::clicked, this, [=]() {
    pause_button_->setEnabled(true);
    replay_->start(2);
  });
  ff2_button_->setEnabled(false);

  ff4_button_ = new QPushButton("4x");
  connect(ff4_button_, &QPushButton::clicked, this, [=]() {
    pause_button_->setEnabled(true);
    replay_->start(4);
  });
  ff4_button_->setEnabled(false);

  ff8_button_ = new QPushButton("8x");
  connect(ff8_button_, &QPushButton::clicked, this, [=]() {
    pause_button_->setEnabled(true);
    replay_->start(8);
  });
  ff8_button_->setEnabled(false);

  QHBoxLayout* layout = new QHBoxLayout;
  layout->addWidget(load_button_);
  layout->addWidget(pause_button_);
  layout->addWidget(play_button_);
  layout->addWidget(ff2_button_);
  layout->addWidget(ff4_button_);
  layout->addWidget(ff8_button_);

  this->setCentralWidget(new QWidget);
  this->centralWidget()->setLayout(layout);
  show();
}

void ReplayWindow::loadFile(QString path) {
  QFile* input = new QFile(path, this);
  input->open(QIODevice::ReadOnly);

  std::vector<Packet> ps(parse(input));

  qInfo("parsed %d", ps.size());
  replay_ = new Replay(this, std::move(ps));

  play_button_->setEnabled(true);
  ff2_button_->setEnabled(true);
  ff4_button_->setEnabled(true);
  ff8_button_->setEnabled(true);
  QTcpServer* server = new QTcpServer(this);
  QObject::connect(server, &QTcpServer::newConnection, [=]() {
    QTcpSocket *sock = server->nextPendingConnection();
    ReplayConnection *conn = new ReplayConnection(this, replay_, sock);
    QObject::connect(sock, &QIODevice::readyRead, conn,
                     &ReplayConnection::handleRead);
  });
  server->listen(QHostAddress::Any, 5678);
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);

  ReplayWindow window;

  app.exec();
}
