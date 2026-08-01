#ifndef MCPSERVER_H
#define MCPSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QByteArray>
#include <QList>

class MCPServer : public QObject
{
    Q_OBJECT
public:
    explicit MCPServer(QObject *parent = nullptr);
    void start(quint16 port = 8888);

signals:
    void clientCountChanged(int count);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onClientDisconnected();

private:
    void processMessage(const QJsonObject &request, QTcpSocket *client);
    void handleToolsList(QJsonObject &response);
    void handleToolsCall(const QJsonObject &request, QJsonObject &response);
    void sendResponse(const QJsonObject &response, QTcpSocket *client);
    
    QTcpServer *tcpServer;
    QList<QTcpSocket*> clients;
};

#endif // MCPSERVER_H
