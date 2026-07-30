#include "mcpserver.h"
#include <QDebug>
#include <QJsonParseError>
#include <QJsonArray>
#include "../mainwindow.h"
#include "../re/frameinfowindow.h"
#include "../utility.h"
#include "../connections/canconmanager.h"

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
        QJsonObject result;
        QJsonArray tools;
        
        QJsonObject pingTool;
        pingTool["name"] = "ping";
        pingTool["description"] = "A simple ping tool";
        
        QJsonObject pingInputSchema;
        pingInputSchema["type"] = "object";
        pingInputSchema["properties"] = QJsonObject();
        pingTool["inputSchema"] = pingInputSchema;
        
        QJsonObject analyzeTool;
        analyzeTool["name"] = "analyze_frame_data";
        analyzeTool["description"] = "Opens the Frame Data Analysis UI for the user and selects the given CAN frame ID.";
        
        QJsonObject analyzeInputSchema;
        analyzeInputSchema["type"] = "object";
        QJsonObject analyzeProperties;
        QJsonObject frameIdProp;
        frameIdProp["type"] = "string";
        frameIdProp["description"] = "The CAN frame ID to analyze (e.g. '01A2').";
        analyzeProperties["frameId"] = frameIdProp;
        analyzeInputSchema["properties"] = analyzeProperties;
        
        QJsonArray required;
        required.append("frameId");
        analyzeInputSchema["required"] = required;
        
        analyzeTool["inputSchema"] = analyzeInputSchema;
        
        QJsonObject queryTool;
        queryTool["name"] = "query_can_logs";
        queryTool["description"] = "Query raw CAN frames from logs/live capture. Set maxResults=0 to just get counts.";
        
        QJsonObject queryInputSchema;
        queryInputSchema["type"] = "object";
        QJsonObject queryProperties;
        
        QJsonObject frameIdProp2;
        frameIdProp2["type"] = "string";
        frameIdProp2["description"] = "Optional CAN frame ID to filter by (e.g. '01A2').";
        queryProperties["frameId"] = frameIdProp2;
        
        QJsonObject busProp;
        busProp["type"] = "integer";
        queryProperties["bus"] = busProp;
        
        QJsonObject minTsProp;
        minTsProp["type"] = "integer";
        minTsProp["description"] = "Optional minimum timestamp (microseconds).";
        queryProperties["minTimestamp"] = minTsProp;
        
        QJsonObject maxTsProp;
        maxTsProp["type"] = "integer";
        queryProperties["maxTimestamp"] = maxTsProp;
        
        QJsonObject sortProp;
        sortProp["type"] = "string";
        sortProp["description"] = "'recent' (default) or 'earliest'";
        sortProp["enum"] = QJsonArray() << "recent" << "earliest";
        queryProperties["sort"] = sortProp;
        
        QJsonObject offsetProp;
        offsetProp["type"] = "integer";
        queryProperties["offset"] = offsetProp;
        
        QJsonObject maxResultsProp;
        maxResultsProp["type"] = "integer";
        maxResultsProp["description"] = "Max frames to return (default 500). Set to 0 to only get counts.";
        queryProperties["maxResults"] = maxResultsProp;
        
        queryInputSchema["properties"] = queryProperties;
        queryTool["inputSchema"] = queryInputSchema;
        
        QJsonObject analysisTool;
        analysisTool["name"] = "query_analysis_tools";
        analysisTool["description"] = "Fetch data from the core analysis tools (Sniffer, FlowView, Bisect).";
        
        QJsonObject analysisInputSchema;
        analysisInputSchema["type"] = "object";
        QJsonObject analysisProperties;
        
        QJsonObject toolProp;
        toolProp["type"] = "string";
        toolProp["description"] = "'sniffer', 'flowview', or 'bisect'";
        toolProp["enum"] = QJsonArray() << "sniffer" << "flowview" << "bisect";
        analysisProperties["tool"] = toolProp;
        
        analysisInputSchema["properties"] = analysisProperties;
        QJsonArray analysisRequired;
        analysisRequired.append("tool");
        analysisInputSchema["required"] = analysisRequired;
        analysisTool["inputSchema"] = analysisInputSchema;
        
        QJsonObject injectTool;
        injectTool["name"] = "inject_can_frame";
        injectTool["description"] = "Inject specific bytes to a specific CAN ID.";
        QJsonObject injectInputSchema;
        injectInputSchema["type"] = "object";
        QJsonObject injectProperties;
        injectProperties["bus"] = QJsonObject({{"type", "integer"}, {"description", "The bus number"}});
        injectProperties["id"] = QJsonObject({{"type", "integer"}, {"description", "The CAN ID (e.g., 0x123 as integer)"}});
        injectProperties["data"] = QJsonObject({{"type", "string"}, {"description", "The hex string of bytes to send (e.g., '12AB34')"}});
        injectInputSchema["properties"] = injectProperties;
        injectInputSchema["required"] = QJsonArray() << "bus" << "id" << "data";
        injectTool["inputSchema"] = injectInputSchema;

        QJsonObject configFuzzerTool;
        configFuzzerTool["name"] = "configure_fuzzer";
        configFuzzerTool["description"] = "Configure fuzzer with ID ranges and mutation rates.";
        QJsonObject configFuzzerSchema;
        configFuzzerSchema["type"] = "object";
        QJsonObject configFuzzerProps;
        configFuzzerProps["startId"] = QJsonObject({{"type", "integer"}});
        configFuzzerProps["endId"] = QJsonObject({{"type", "integer"}});
        configFuzzerProps["intervalMs"] = QJsonObject({{"type", "integer"}});
        configFuzzerProps["fuzzType"] = QJsonObject({{"type", "integer"}, {"description", "0=Sequential, 1=Sweeping, 2=Random"}});
        configFuzzerSchema["properties"] = configFuzzerProps;
        configFuzzerSchema["required"] = QJsonArray() << "startId" << "endId" << "intervalMs" << "fuzzType";
        configFuzzerTool["inputSchema"] = configFuzzerSchema;

        QJsonObject startFuzzerTool;
        startFuzzerTool["name"] = "start_fuzzer";
        startFuzzerTool["description"] = "Start the configured fuzzer.";
        startFuzzerTool["inputSchema"] = QJsonObject({{"type", "object"}, {"properties", QJsonObject()}});

        QJsonObject stopFuzzerTool;
        stopFuzzerTool["name"] = "stop_fuzzer";
        stopFuzzerTool["description"] = "Stop the running fuzzer.";
        stopFuzzerTool["inputSchema"] = QJsonObject({{"type", "object"}, {"properties", QJsonObject()}});
        
        tools.append(pingTool);
        tools.append(analyzeTool);
        tools.append(queryTool);
        tools.append(analysisTool);
        tools.append(injectTool);
        tools.append(configFuzzerTool);
        tools.append(startFuzzerTool);
        tools.append(stopFuzzerTool);
        result["tools"] = tools;
        response["result"] = result;
    }
    else if (method == "tools/call") {
        QJsonObject params = request["params"].toObject();
        QString toolName = params["name"].toString();
        
        QJsonObject result;
        QJsonArray content;
        
        if (toolName == "ping") {
            QJsonObject item;
            item["type"] = "text";
            item["text"] = "pong";
            content.append(item);
        } else if (toolName == "analyze_frame_data") {
            QString frameId = params["arguments"].toObject()["frameId"].toString();
            
            // This runs on the main GUI thread, so it's safe to manipulate the UI
            MainWindow::getReference()->analyzeFrameData(frameId);
            
            uint32_t idInt = Utility::ParseStringToNum(frameId);
            const QVector<CANFrame> *frames = MainWindow::getReference()->getCANFrameModel()->getListReference();
            QJsonArray matchedFrames;
            int count = 0;
            for (int i = frames->size() - 1; i >= 0 && count < 50; --i) {
                if (frames->at(i).frameId() == idInt) {
                    QJsonObject frameObj;
                    frameObj["timestamp"] = (qint64)(frames->at(i).timeStamp().seconds() * 1000000 + frames->at(i).timeStamp().microSeconds());
                    frameObj["bus"] = frames->at(i).bus;
                    frameObj["data_hex"] = QString(frames->at(i).payload().toHex());
                    matchedFrames.insert(0, frameObj);
                    count++;
                }
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = "Successfully opened Frame Data Analysis for ID " + frameId + ".\nHere are the " + QString::number(matchedFrames.size()) + " most recent frames for this ID:\n" + QJsonDocument(matchedFrames).toJson(QJsonDocument::Indented);
            content.append(item);
            
            FrameInfoWindow* fiw = MainWindow::getReference()->getFrameInfoWindow();
            if (fiw) {
                QJsonObject statsItem;
                statsItem["type"] = "text";
                statsItem["text"] = "And here are the computation statistics computed by the application:\n" + QString::fromUtf8(QJsonDocument(fiw->getStatisticsAsJson()).toJson(QJsonDocument::Indented));
                content.append(statsItem);
            }
            
        } else if (toolName == "query_can_logs") {
            QJsonObject args = params["arguments"].toObject();
            
            bool filterId = args.contains("frameId");
            uint32_t targetId = 0;
            if (filterId) {
                targetId = Utility::ParseStringToNum(args["frameId"].toString());
            }
            
            bool filterBus = args.contains("bus");
            int targetBus = args["bus"].toInt();
            
            bool filterMinTs = args.contains("minTimestamp");
            qint64 minTs = filterMinTs ? args["minTimestamp"].toDouble() : 0;
            
            bool filterMaxTs = args.contains("maxTimestamp");
            qint64 maxTs = filterMaxTs ? args["maxTimestamp"].toDouble() : 0;
            
            QString sort = args.contains("sort") ? args["sort"].toString() : "recent";
            int offset = args.contains("offset") ? args["offset"].toInt() : 0;
            
            int maxResults = args.contains("maxResults") ? args["maxResults"].toInt() : 500;
            if (maxResults > 5000) maxResults = 5000;
            
            const QVector<CANFrame> *frames = MainWindow::getReference()->getCANFrameModel()->getListReference();
            
            QVector<int> matchedIndices;
            for (int i = 0; i < frames->size(); ++i) {
                const CANFrame &f = frames->at(i);
                if (filterId && f.frameId() != targetId) continue;
                if (filterBus && f.bus != targetBus) continue;
                
                qint64 ts = (qint64)(f.timeStamp().seconds() * 1000000 + f.timeStamp().microSeconds());
                if (filterMinTs && ts < minTs) continue;
                if (filterMaxTs && ts > maxTs) continue;
                
                matchedIndices.append(i);
            }
            
            int totalMatching = matchedIndices.size();
            QJsonArray matchedFrames;
            
            if (maxResults > 0) {
                if (sort == "recent") {
                    for (int i = matchedIndices.size() - 1 - offset; i >= 0 && matchedFrames.size() < maxResults; --i) {
                        const CANFrame &f = frames->at(matchedIndices[i]);
                        QJsonObject frameObj;
                        frameObj["timestamp"] = (qint64)(f.timeStamp().seconds() * 1000000 + f.timeStamp().microSeconds());
                        frameObj["bus"] = f.bus;
                        frameObj["id"] = QString::number(f.frameId(), 16).toUpper();
                        frameObj["data_hex"] = QString(f.payload().toHex());
                        matchedFrames.append(frameObj);
                    }
                } else {
                    for (int i = offset; i < matchedIndices.size() && matchedFrames.size() < maxResults; ++i) {
                        const CANFrame &f = frames->at(matchedIndices[i]);
                        QJsonObject frameObj;
                        frameObj["timestamp"] = (qint64)(f.timeStamp().seconds() * 1000000 + f.timeStamp().microSeconds());
                        frameObj["bus"] = f.bus;
                        frameObj["id"] = QString::number(f.frameId(), 16).toUpper();
                        frameObj["data_hex"] = QString(f.payload().toHex());
                        matchedFrames.append(frameObj);
                    }
                }
            }
            
            QJsonObject resultData;
            resultData["totalFramesInLog"] = frames->size();
            resultData["totalMatchingFilters"] = totalMatching;
            resultData["returnedCount"] = matchedFrames.size();
            resultData["data"] = matchedFrames;
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "query_analysis_tools") {
            QString target = params["arguments"].toObject()["tool"].toString();
            QJsonObject resultData;
            
            if (target == "sniffer") {
                if (MainWindow::getReference()->getSnifferWindow()) {
                     resultData = MainWindow::getReference()->getSnifferWindow()->getSnifferData();
                } else {
                     resultData["error"] = "Sniffer window not open or unavailable";
                }
            } else if (target == "flowview") {
                if (MainWindow::getReference()->getFlowViewWindow()) {
                     resultData = MainWindow::getReference()->getFlowViewWindow()->getFlowViewStats();
                } else {
                     resultData["error"] = "Flow View window not open or unavailable";
                }
            } else if (target == "bisect") {
                if (MainWindow::getReference()->getBisectWindow()) {
                     resultData = MainWindow::getReference()->getBisectWindow()->getBisectStatus();
                } else {
                     resultData["error"] = "Bisect window not open or unavailable";
                }
            } else {
                resultData["error"] = "Unknown analysis tool requested";
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "inject_can_frame") {
            int bus = params["arguments"].toObject()["bus"].toInt();
            int id = params["arguments"].toObject()["id"].toInt();
            QString dataStr = params["arguments"].toObject()["data"].toString();
            QByteArray data = QByteArray::fromHex(dataStr.toUtf8());
            
            CANFrame frame;
            frame.bus = bus;
            frame.setFrameId(id);
            frame.setPayload(data);
            frame.setExtendedFrameFormat(id > 0x7FF);
            
            bool success = CANConManager::getInstance()->sendFrame(frame);
            
            QJsonObject resultData;
            resultData["success"] = success;
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "configure_fuzzer") {
            int startId = params["arguments"].toObject()["startId"].toInt();
            int endId = params["arguments"].toObject()["endId"].toInt();
            int intervalMs = params["arguments"].toObject()["intervalMs"].toInt();
            int fuzzType = params["arguments"].toObject()["fuzzType"].toInt();
            
            QJsonObject resultData;
            if (MainWindow::getReference()->getFuzzingWindow()) {
                MainWindow::getReference()->getFuzzingWindow()->mcpConfigure(startId, endId, intervalMs, fuzzType);
                resultData["success"] = true;
            } else {
                resultData["error"] = "Fuzzing window not open";
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "start_fuzzer") {
            QJsonObject resultData;
            if (MainWindow::getReference()->getFuzzingWindow()) {
                MainWindow::getReference()->getFuzzingWindow()->mcpStart();
                resultData["success"] = true;
            } else {
                resultData["error"] = "Fuzzing window not open";
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "stop_fuzzer") {
            QJsonObject resultData;
            if (MainWindow::getReference()->getFuzzingWindow()) {
                MainWindow::getReference()->getFuzzingWindow()->mcpStop();
                resultData["success"] = true;
            } else {
                resultData["error"] = "Fuzzing window not open";
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else {
            result["isError"] = true;
            QJsonObject item;
            item["type"] = "text";
            item["text"] = "Unknown tool";
            content.append(item);
        }
        
        result["content"] = content;
        response["result"] = result;
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
