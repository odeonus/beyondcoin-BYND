#ifndef CHATBOXPAGE_H
#define CHATBOXPAGE_H

#include "bitcoingui.h"

#include <QWidget>
#include <QMainWindow>
#include <QObject>

#include <QTcpSocket>


namespace Ui
{
  class ChatboxPage;
}

class ChatboxPage : public QWidget
{
  Q_OBJECT

public:
  ChatboxPage(QWidget *parent = 0);

private Q_SLOTS:
  // This function gets called when a user clicks on the
  // loginButton on the front page (which you placed there
  // with Designer)
  void on_loginButton_clicked();
  void on_logoutButton_clicked();

  // This gets called when you click the sayButton on
  // the chat page.
  void on_sayButton_clicked();

  // This is a function we'll connect to a socket's readyRead()
  // signal, which tells us there's text to be read from the chat
  // server.
  void readyRead();

  // This function gets called when the socket tells us it's connected.
  void connected();
  void disconnected();

protected:

private:
  Ui::ChatboxPage *ui;
  // This is the socket that will let us communitate with the server.
  QTcpSocket *socket;

  void loadSettings();
  void saveSettings();
  QString chatserver,username;


Q_SIGNALS:
};

#endif // BITCOIN_QT_CHATWINDOW_H
