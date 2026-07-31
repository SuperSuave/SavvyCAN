#include "mcpserver.h"
#include <QDebug>
#include <QJsonParseError>
#include <QJsonArray>
#include "../mainwindow.h"
#include "../re/frameinfowindow.h"
#include "../utility.h"
#include "../connections/canconmanager.h"
#include "../dbc/dbchandler.h"
#include "../connections/connectionwindow.h"
#include "../frameplaybackwindow.h"
#include "../re/udsscanwindow.h"
#include "../re/isotp_interpreterwindow.h"
#include "../re/graphingwindow.h"
#include "../signalviewerwindow.h"
#include "../framesenderwindow.h"

MCPServer::MCPServer(QObject *parent) : QObject(parent), tcpServer(new QTcpServer(this))
{
}

void MCPServer::start(quint16 port)
{
    if (tcpServer->listen(QHostAddress::Any, port)) {
        connect(tcpServer, &QTcpServer::newConnection, this, &MCPServer::onNewConnection);
        qDebug() << "MCP Server listening on TCP port" << port;
    } else {
        qDebug() << "MCP Server failed to start on TCP port" << port;
    }
}

void MCPServer::onNewConnection()
{
    QTcpSocket *client = tcpServer->nextPendingConnection();
    connect(client, &QTcpSocket::readyRead, this, &MCPServer::onReadyRead);
    connect(client, &QTcpSocket::disconnected, this, &MCPServer::onClientDisconnected);
    clients.append(client);
    emit clientCountChanged(clients.size());
    qDebug() << "MCP Server: New AI client connected.";
}

void MCPServer::onClientDisconnected()
{
    QTcpSocket *client = qobject_cast<QTcpSocket *>(sender());
    if (client) {
        clients.removeAll(client);
        client->deleteLater();
        emit clientCountChanged(clients.size());
        qDebug() << "MCP Server: AI client disconnected.";
    }
}

void MCPServer::onReadyRead()
{
    QTcpSocket *client = qobject_cast<QTcpSocket *>(sender());
    if (!client) return;
    
    while (client->canReadLine()) {
        QByteArray line = client->readLine().trimmed();
        if (line.isEmpty()) continue;
        
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            processMessage(doc.object(), client);
        }
    }
}

void MCPServer::processMessage(const QJsonObject &request, QTcpSocket *client)
{
    QString method = request["method"].toString();
    QJsonValue id = request["id"];
    
    QJsonObject response;
    response["jsonrpc"] = "2.0";
    if (!id.isUndefined()) {
        response["id"] = id;
    }
    
    if (method == "initialize") {
        QJsonObject result;
        result["protocolVersion"] = "2024-11-05";
        
        QJsonObject capabilities;
        QJsonObject tools;
        capabilities["tools"] = tools;
        result["capabilities"] = capabilities;
        
        QJsonObject serverInfo;
        serverInfo["name"] = "savvylens-mcp";
        serverInfo["version"] = "1.0.0";
        result["serverInfo"] = serverInfo;
        
        response["result"] = result;
    } 
    else if (method == "notifications/initialized") {
        // Just an ack, no response needed
        return;
    }
    else if (method == "tools/list") {
        handleToolsList(response);
    }
    else if (method == "tools/call") {
        handleToolsCall(request, response);
    }
    
    if (!id.isUndefined()) {
        sendResponse(response, client);
    }
}

void MCPServer::sendResponse(const QJsonObject &response, QTcpSocket *client)
{
    QJsonDocument doc(response);
    QByteArray data = doc.toJson(QJsonDocument::Compact);
    data.append("\n");
    
    if (client && client->isOpen()) {
        client->write(data);
        client->flush();
    }
}
