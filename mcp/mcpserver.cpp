#include "mcpserver.h"
#include <QDebug>
#include <QJsonParseError>
#include <QJsonArray>
#include "../mainwindow.h"
#include "../re/frameinfowindow.h"
#include "../utility.h"
#include "../connections/canconmanager.h"
#include "../dbc/dbchandler.h"
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
        
        QJsonObject queryDbcTool;
        queryDbcTool["name"] = "query_dbc_signal";
        queryDbcTool["description"] = "Fetch definitions of specific signals by name or ID.";
        QJsonObject queryDbcSchema;
        queryDbcSchema["type"] = "object";
        QJsonObject queryDbcProps;
        queryDbcProps["id"] = QJsonObject({{"type", "integer"}, {"description", "The CAN ID of the message"}});
        queryDbcProps["signal_name"] = QJsonObject({{"type", "string"}, {"description", "Optional signal name to fetch specifically"}});
        queryDbcSchema["properties"] = queryDbcProps;
        queryDbcSchema["required"] = QJsonArray() << "id";
        queryDbcTool["inputSchema"] = queryDbcSchema;

        QJsonObject parseFrameTool;
        parseFrameTool["name"] = "parse_can_frame";
        parseFrameTool["description"] = "Parse a raw CAN frame using the active DBC files.";
        QJsonObject parseFrameSchema;
        parseFrameSchema["type"] = "object";
        QJsonObject parseFrameProps;
        parseFrameProps["id"] = QJsonObject({{"type", "integer"}});
        parseFrameProps["data"] = QJsonObject({{"type", "string"}, {"description", "Hex string of payload"}});
        parseFrameSchema["properties"] = parseFrameProps;
        parseFrameSchema["required"] = QJsonArray() << "id" << "data";
        parseFrameTool["inputSchema"] = parseFrameSchema;

        QJsonObject manageNodeTool;
        manageNodeTool["name"] = "manage_dbc_node";
        manageNodeTool["description"] = "Add, edit, or remove a DBC node.";
        QJsonObject manageNodeSchema;
        manageNodeSchema["type"] = "object";
        QJsonObject manageNodeProps;
        manageNodeProps["action"] = QJsonObject({{"type", "string"}, {"enum", QJsonArray() << "add" << "edit" << "remove"}});
        manageNodeProps["name"] = QJsonObject({{"type", "string"}});
        manageNodeProps["newName"] = QJsonObject({{"type", "string"}, {"description", "For edit action"}});
        manageNodeProps["comment"] = QJsonObject({{"type", "string"}});
        manageNodeSchema["properties"] = manageNodeProps;
        manageNodeSchema["required"] = QJsonArray() << "action" << "name";
        manageNodeTool["inputSchema"] = manageNodeSchema;

        QJsonObject manageMsgTool;
        manageMsgTool["name"] = "manage_dbc_message";
        manageMsgTool["description"] = "Add, edit, or remove a DBC message.";
        QJsonObject manageMsgSchema;
        manageMsgSchema["type"] = "object";
        QJsonObject manageMsgProps;
        manageMsgProps["action"] = QJsonObject({{"type", "string"}, {"enum", QJsonArray() << "add" << "edit" << "remove"}});
        manageMsgProps["id"] = QJsonObject({{"type", "integer"}});
        manageMsgProps["name"] = QJsonObject({{"type", "string"}});
        manageMsgProps["len"] = QJsonObject({{"type", "integer"}});
        manageMsgProps["sender"] = QJsonObject({{"type", "string"}, {"description", "Name of the sender node"}});
        manageMsgSchema["properties"] = manageMsgProps;
        manageMsgSchema["required"] = QJsonArray() << "action" << "id";
        manageMsgTool["inputSchema"] = manageMsgSchema;

        QJsonObject manageSigTool;
        manageSigTool["name"] = "manage_dbc_signal";
        manageSigTool["description"] = "Add, edit, or remove a DBC signal.";
        QJsonObject manageSigSchema;
        manageSigSchema["type"] = "object";
        QJsonObject manageSigProps;
        manageSigProps["action"] = QJsonObject({{"type", "string"}, {"enum", QJsonArray() << "add" << "edit" << "remove"}});
        manageSigProps["messageId"] = QJsonObject({{"type", "integer"}});
        manageSigProps["name"] = QJsonObject({{"type", "string"}});
        manageSigProps["startBit"] = QJsonObject({{"type", "integer"}});
        manageSigProps["size"] = QJsonObject({{"type", "integer"}});
        manageSigProps["isLittleEndian"] = QJsonObject({{"type", "boolean"}});
        manageSigProps["isSigned"] = QJsonObject({{"type", "boolean"}});
        manageSigProps["factor"] = QJsonObject({{"type", "number"}});
        manageSigProps["bias"] = QJsonObject({{"type", "number"}});
        manageSigSchema["properties"] = manageSigProps;
        manageSigSchema["required"] = QJsonArray() << "action" << "messageId" << "name";
        manageSigTool["inputSchema"] = manageSigSchema;

        QJsonObject manageFileTool;
        manageFileTool["name"] = "manage_dbc_file";
        manageFileTool["description"] = "Create, load, or save a DBC file.";
        QJsonObject manageFileSchema;
        manageFileSchema["type"] = "object";
        QJsonObject manageFileProps;
        manageFileProps["action"] = QJsonObject({{"type", "string"}, {"enum", QJsonArray() << "create" << "load" << "save"}});
        manageFileProps["filename"] = QJsonObject({{"type", "string"}, {"description", "Absolute path to the DBC file"}});
        manageFileSchema["properties"] = manageFileProps;
        manageFileSchema["required"] = QJsonArray() << "action";
        manageFileTool["inputSchema"] = manageFileSchema;
        
        QJsonObject openUdsTool;
        openUdsTool["name"] = "open_uds_scanner";
        openUdsTool["description"] = "Open and configure the UDS Scanner window.";
        QJsonObject openUdsSchema;
        openUdsSchema["type"] = "object";
        QJsonObject openUdsProps;
        openUdsProps["startId"] = QJsonObject({{"type", "integer"}});
        openUdsProps["endId"] = QJsonObject({{"type", "integer"}});
        openUdsProps["bus"] = QJsonObject({{"type", "integer"}});
        openUdsProps["scanType"] = QJsonObject({{"type", "integer"}});
        openUdsSchema["properties"] = openUdsProps;
        openUdsSchema["required"] = QJsonArray() << "startId" << "endId" << "bus" << "scanType";
        openUdsTool["inputSchema"] = openUdsSchema;
        
        QJsonObject startUdsTool;
        startUdsTool["name"] = "start_uds_scan";
        startUdsTool["description"] = "Start the UDS Scanner.";
        startUdsTool["inputSchema"] = QJsonObject({{"type", "object"}, {"properties", QJsonObject()}});
        
        QJsonObject stopUdsTool;
        stopUdsTool["name"] = "stop_uds_scan";
        stopUdsTool["description"] = "Stop the UDS Scanner.";
        stopUdsTool["inputSchema"] = QJsonObject({{"type", "object"}, {"properties", QJsonObject()}});
        
        QJsonObject openIsotpTool;
        openIsotpTool["name"] = "open_isotp_interpreter";
        openIsotpTool["description"] = "Open the ISO-TP Interpreter window.";
        QJsonObject openIsotpSchema;
        openIsotpSchema["type"] = "object";
        QJsonObject openIsotpProps;
        openIsotpProps["rxId"] = QJsonObject({{"type", "integer"}});
        openIsotpSchema["properties"] = openIsotpProps;
        openIsotpSchema["required"] = QJsonArray() << "rxId";
        openIsotpTool["inputSchema"] = openIsotpSchema;
        
        QJsonObject addFrameSenderTool;
        addFrameSenderTool["name"] = "add_frame_sender_sequence";
        addFrameSenderTool["description"] = "Add a sequence to the Frame Sender window.";
        QJsonObject addFrameSenderSchema;
        addFrameSenderSchema["type"] = "object";
        QJsonObject addFrameSenderProps;
        addFrameSenderProps["bus"] = QJsonObject({{"type", "integer"}});
        addFrameSenderProps["id"] = QJsonObject({{"type", "integer"}});
        addFrameSenderProps["data"] = QJsonObject({{"type", "string"}});
        addFrameSenderProps["intervalMs"] = QJsonObject({{"type", "integer"}});
        addFrameSenderSchema["properties"] = addFrameSenderProps;
        addFrameSenderSchema["required"] = QJsonArray() << "bus" << "id" << "data" << "intervalMs";
        addFrameSenderTool["inputSchema"] = addFrameSenderSchema;
        
        QJsonObject openSignalViewerTool;
        openSignalViewerTool["name"] = "open_signal_viewer";
        openSignalViewerTool["description"] = "Open a signal in the Signal Viewer window.";
        QJsonObject openSignalViewerSchema;
        openSignalViewerSchema["type"] = "object";
        QJsonObject openSignalViewerProps;
        openSignalViewerProps["messageId"] = QJsonObject({{"type", "integer"}});
        openSignalViewerProps["signalName"] = QJsonObject({{"type", "string"}});
        openSignalViewerSchema["properties"] = openSignalViewerProps;
        openSignalViewerSchema["required"] = QJsonArray() << "messageId" << "signalName";
        openSignalViewerTool["inputSchema"] = openSignalViewerSchema;
        
        QJsonObject openGraphTool;
        openGraphTool["name"] = "open_graph";
        openGraphTool["description"] = "Open a signal in the Graphing window.";
        QJsonObject openGraphSchema;
        openGraphSchema["type"] = "object";
        QJsonObject openGraphProps;
        openGraphProps["messageId"] = QJsonObject({{"type", "integer"}});
        openGraphProps["signalName"] = QJsonObject({{"type", "string"}});
        openGraphSchema["properties"] = openGraphProps;
        openGraphSchema["required"] = QJsonArray() << "messageId" << "signalName";
        openGraphTool["inputSchema"] = openGraphSchema;
        
        tools.append(pingTool);
        tools.append(analyzeTool);
        tools.append(queryTool);
        tools.append(analysisTool);
        tools.append(injectTool);
        tools.append(configFuzzerTool);
        tools.append(startFuzzerTool);
        tools.append(stopFuzzerTool);
        tools.append(queryDbcTool);
        tools.append(parseFrameTool);
        tools.append(manageNodeTool);
        tools.append(manageMsgTool);
        tools.append(manageSigTool);
        tools.append(manageFileTool);
        tools.append(openUdsTool);
        tools.append(startUdsTool);
        tools.append(stopUdsTool);
        tools.append(openIsotpTool);
        tools.append(addFrameSenderTool);
        tools.append(openSignalViewerTool);
        tools.append(openGraphTool);
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
            
        } else if (toolName == "query_dbc_signal") {
            int id = params["arguments"].toObject()["id"].toInt();
            QString signalName = params["arguments"].toObject()["signal_name"].toString();
            
            QJsonObject resultData;
            DBCHandler *handler = DBCHandler::getReference();
            DBC_MESSAGE *msg = handler->findMessage(id);
            
            if (!msg) {
                resultData["error"] = "Message ID not found in DBC";
            } else {
                resultData["message_name"] = msg->name;
                QJsonArray signalsArray;
                if (!signalName.isEmpty()) {
                    DBC_SIGNAL *sig = msg->sigHandler->findSignalByName(signalName);
                    if (sig) {
                        QJsonObject sigObj;
                        sigObj["name"] = sig->name;
                        sigObj["startBit"] = sig->startBit;
                        sigObj["size"] = sig->signalSize;
                        sigObj["isLittleEndian"] = sig->intelByteOrder;
                        sigObj["isSigned"] = (sig->valType == SIGNED_INT);
                        sigObj["factor"] = sig->factor;
                        sigObj["bias"] = sig->bias;
                        signalsArray.append(sigObj);
                    }
                } else {
                    for (int i = 0; i < msg->sigHandler->getCount(); i++) {
                        DBC_SIGNAL *sig = msg->sigHandler->findSignalByIdx(i);
                        if (sig) {
                            QJsonObject sigObj;
                            sigObj["name"] = sig->name;
                            sigObj["startBit"] = sig->startBit;
                            sigObj["size"] = sig->signalSize;
                            sigObj["isLittleEndian"] = sig->intelByteOrder;
                            sigObj["isSigned"] = (sig->valType == SIGNED_INT);
                            sigObj["factor"] = sig->factor;
                            sigObj["bias"] = sig->bias;
                            signalsArray.append(sigObj);
                        }
                    }
                }
                resultData["signals"] = signalsArray;
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);

        } else if (toolName == "parse_can_frame") {
            int id = params["arguments"].toObject()["id"].toInt();
            QString dataStr = params["arguments"].toObject()["data"].toString();
            QByteArray data = QByteArray::fromHex(dataStr.toUtf8());
            
            CANFrame frame;
            frame.setFrameId(id);
            frame.setPayload(data);
            
            DBCHandler *handler = DBCHandler::getReference();
            DBC_MESSAGE *msg = handler->findMessage(id);
            QJsonObject resultData;
            
            if (!msg) {
                resultData["error"] = "Message ID not found in DBC";
            } else {
                resultData["message_name"] = msg->name;
                QJsonArray signalsArray;
                for (int i = 0; i < msg->sigHandler->getCount(); i++) {
                    DBC_SIGNAL *sig = msg->sigHandler->findSignalByIdx(i);
                    if (sig) {
                        double outValue = 0;
                        if (sig->processAsDouble(frame, outValue)) {
                            QJsonObject sigObj;
                            sigObj["name"] = sig->name;
                            sigObj["value"] = outValue;
                            QString textVal;
                            if (sig->processAsText(frame, textVal, false, true)) {
                                sigObj["text"] = textVal;
                            }
                            signalsArray.append(sigObj);
                        }
                    }
                }
                resultData["signals"] = signalsArray;
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);

        } else if (toolName == "manage_dbc_node") {
            QString action = params["arguments"].toObject()["action"].toString();
            QString name = params["arguments"].toObject()["name"].toString();
            
            DBCHandler *handler = DBCHandler::getReference();
            if (handler->getFileCount() == 0) handler->createBlankFile();
            DBCFile *file = handler->getFileByIdx(0);
            
            QJsonObject resultData;
            if (action == "add") {
                if (file->findNodeByName(name)) {
                    resultData["error"] = "Node already exists";
                } else {
                    DBC_NODE node;
                    node.name = name;
                    if (params["arguments"].toObject().contains("comment")) {
                        node.comment = params["arguments"].toObject()["comment"].toString();
                    }
                    file->dbc_nodes.append(node);
                    resultData["success"] = true;
                }
            } else if (action == "edit") {
                DBC_NODE *node = file->findNodeByName(name);
                if (node) {
                    if (params["arguments"].toObject().contains("newName")) {
                        node->name = params["arguments"].toObject()["newName"].toString();
                    }
                    if (params["arguments"].toObject().contains("comment")) {
                        node->comment = params["arguments"].toObject()["comment"].toString();
                    }
                    resultData["success"] = true;
                } else {
                    resultData["error"] = "Node not found";
                }
            } else if (action == "remove") {
                bool found = false;
                for (int i=0; i<file->dbc_nodes.count(); i++) {
                    if (file->dbc_nodes[i].name == name) {
                        file->dbc_nodes.removeAt(i);
                        found = true;
                        break;
                    }
                }
                if (found) resultData["success"] = true;
                else resultData["error"] = "Node not found";
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);

        } else if (toolName == "manage_dbc_message") {
            QString action = params["arguments"].toObject()["action"].toString();
            int id = params["arguments"].toObject()["id"].toInt();
            
            DBCHandler *handler = DBCHandler::getReference();
            if (handler->getFileCount() == 0) handler->createBlankFile();
            DBCFile *file = handler->getFileByIdx(0);
            
            QJsonObject resultData;
            if (action == "add") {
                if (handler->findMessage(id)) {
                    resultData["error"] = "Message already exists";
                } else {
                    DBC_MESSAGE msg;
                    msg.ID = id;
                    msg.name = params["arguments"].toObject().contains("name") ? params["arguments"].toObject()["name"].toString() : QString("MSG_") + QString::number(id, 16).toUpper();
                    msg.len = params["arguments"].toObject().contains("len") ? params["arguments"].toObject()["len"].toInt() : 8;
                    if (params["arguments"].toObject().contains("sender")) {
                        DBC_NODE *sender = file->findNodeByName(params["arguments"].toObject()["sender"].toString());
                        msg.sender = sender;
                    }
                    file->messageHandler->addMessage(msg);
                    resultData["success"] = true;
                }
            } else if (action == "edit") {
                DBC_MESSAGE *msg = handler->findMessage(id);
                if (msg) {
                    if (params["arguments"].toObject().contains("name")) {
                        msg->name = params["arguments"].toObject()["name"].toString();
                    }
                    if (params["arguments"].toObject().contains("len")) {
                        msg->len = params["arguments"].toObject()["len"].toInt();
                    }
                    if (params["arguments"].toObject().contains("sender")) {
                        DBC_NODE *sender = file->findNodeByName(params["arguments"].toObject()["sender"].toString());
                        msg->sender = sender;
                    }
                    resultData["success"] = true;
                } else {
                    resultData["error"] = "Message not found";
                }
            } else if (action == "remove") {
                if (file->messageHandler->removeMessage(id)) {
                    resultData["success"] = true;
                } else {
                    resultData["error"] = "Message not found";
                }
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);

        } else if (toolName == "manage_dbc_signal") {
            QString action = params["arguments"].toObject()["action"].toString();
            int id = params["arguments"].toObject()["messageId"].toInt();
            QString name = params["arguments"].toObject()["name"].toString();
            
            DBCHandler *handler = DBCHandler::getReference();
            DBC_MESSAGE *msg = handler->findMessage(id);
            QJsonObject resultData;
            
            if (action == "add" || action == "edit") {
                if (!msg) {
                    if (handler->getFileCount() == 0) handler->createBlankFile();
                    DBCFile *file = handler->getFileByIdx(0);
                    DBC_MESSAGE newMsg;
                    newMsg.ID = id;
                    newMsg.name = QString("MSG_") + QString::number(id, 16).toUpper();
                    newMsg.len = 8;
                    file->messageHandler->addMessage(newMsg);
                    msg = handler->findMessage(id);
                }
                
                DBC_SIGNAL *sig = msg->sigHandler->findSignalByName(name);
                if (action == "add" && sig) {
                    resultData["error"] = "Signal already exists";
                } else if (action == "edit" && !sig) {
                    resultData["error"] = "Signal not found";
                } else {
                    if (action == "add") {
                        DBC_SIGNAL newSig;
                        newSig.name = name;
                        newSig.startBit = params["arguments"].toObject()["startBit"].toInt();
                        newSig.signalSize = params["arguments"].toObject()["size"].toInt();
                        newSig.intelByteOrder = params["arguments"].toObject()["isLittleEndian"].toBool();
                        newSig.valType = params["arguments"].toObject()["isSigned"].toBool() ? SIGNED_INT : UNSIGNED_INT;
                        newSig.factor = params["arguments"].toObject().contains("factor") ? params["arguments"].toObject()["factor"].toDouble() : 1.0;
                        newSig.bias = params["arguments"].toObject().contains("bias") ? params["arguments"].toObject()["bias"].toDouble() : 0.0;
                        msg->sigHandler->addSignal(newSig);
                    } else {
                        if (params["arguments"].toObject().contains("startBit")) sig->startBit = params["arguments"].toObject()["startBit"].toInt();
                        if (params["arguments"].toObject().contains("size")) sig->signalSize = params["arguments"].toObject()["size"].toInt();
                        if (params["arguments"].toObject().contains("isLittleEndian")) sig->intelByteOrder = params["arguments"].toObject()["isLittleEndian"].toBool();
                        if (params["arguments"].toObject().contains("isSigned")) sig->valType = params["arguments"].toObject()["isSigned"].toBool() ? SIGNED_INT : UNSIGNED_INT;
                        if (params["arguments"].toObject().contains("factor")) sig->factor = params["arguments"].toObject()["factor"].toDouble();
                        if (params["arguments"].toObject().contains("bias")) sig->bias = params["arguments"].toObject()["bias"].toDouble();
                    }
                    resultData["success"] = true;
                }
            } else if (action == "remove") {
                if (msg) {
                    if (msg->sigHandler->removeSignal(name)) {
                        resultData["success"] = true;
                    } else {
                        resultData["error"] = "Signal not found";
                    }
                } else {
                    resultData["error"] = "Message not found";
                }
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);

        } else if (toolName == "manage_dbc_file") {
            QString action = params["arguments"].toObject()["action"].toString();
            QString filename = params["arguments"].toObject()["filename"].toString();
            
            DBCHandler *handler = DBCHandler::getReference();
            QJsonObject resultData;
            
            if (action == "create") {
                handler->createBlankFile();
                resultData["success"] = true;
            } else if (action == "load") {
                if (handler->loadDBCFile(filename)) {
                    resultData["success"] = true;
                } else {
                    resultData["error"] = "Failed to load DBC file";
                }
            } else if (action == "save") {
                if (handler->getFileCount() > 0) {
                    DBCFile *file = handler->getFileByIdx(0);
                    if (file->saveFile(filename)) {
                        resultData["success"] = true;
                    } else {
                        resultData["error"] = "Failed to save DBC file";
                    }
                } else {
                    resultData["error"] = "No DBC files currently loaded";
                }
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "open_uds_scanner") {
            int startId = params["arguments"].toObject()["startId"].toInt();
            int endId = params["arguments"].toObject()["endId"].toInt();
            int bus = params["arguments"].toObject()["bus"].toInt();
            int scanType = params["arguments"].toObject()["scanType"].toInt();
            
            QJsonObject resultData;
            if (MainWindow::getReference()->getUDSScanWindow()) {
                MainWindow::getReference()->getUDSScanWindow()->mcpOpenAndConfigure(startId, endId, bus, scanType);
                resultData["success"] = true;
            } else {
                resultData["error"] = "UDS Scanner window not open or unavailable";
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "start_uds_scan") {
            QJsonObject resultData;
            if (MainWindow::getReference()->getUDSScanWindow()) {
                MainWindow::getReference()->getUDSScanWindow()->mcpStartScan();
                resultData["success"] = true;
            } else {
                resultData["error"] = "UDS Scanner window not open or unavailable";
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "stop_uds_scan") {
            QJsonObject resultData;
            if (MainWindow::getReference()->getUDSScanWindow()) {
                MainWindow::getReference()->getUDSScanWindow()->mcpStopScan();
                resultData["success"] = true;
            } else {
                resultData["error"] = "UDS Scanner window not open or unavailable";
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "open_isotp_interpreter") {
            int rxId = params["arguments"].toObject()["rxId"].toInt();
            
            QJsonObject resultData;
            if (MainWindow::getReference()->getISOTPWindow()) {
                MainWindow::getReference()->getISOTPWindow()->mcpOpenAndConfigure(rxId);
                resultData["success"] = true;
            } else {
                resultData["error"] = "ISO-TP window not open or unavailable";
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "add_frame_sender_sequence") {
            int bus = params["arguments"].toObject()["bus"].toInt();
            int id = params["arguments"].toObject()["id"].toInt();
            QString dataStr = params["arguments"].toObject()["data"].toString();
            QByteArray data = QByteArray::fromHex(dataStr.toUtf8());
            int intervalMs = params["arguments"].toObject()["intervalMs"].toInt();
            
            QJsonObject resultData;
            if (MainWindow::getReference()->getFrameSenderWindow()) {
                MainWindow::getReference()->getFrameSenderWindow()->mcpOpenAndAddSequence(bus, id, data, intervalMs);
                resultData["success"] = true;
            } else {
                resultData["error"] = "Frame Sender window not open or unavailable";
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "open_signal_viewer") {
            int messageId = params["arguments"].toObject()["messageId"].toInt();
            QString signalName = params["arguments"].toObject()["signalName"].toString();
            
            QJsonObject resultData;
            if (MainWindow::getReference()->getSignalViewerWindow()) {
                MainWindow::getReference()->getSignalViewerWindow()->mcpOpenForSignal(messageId, signalName);
                resultData["success"] = true;
            } else {
                resultData["error"] = "Signal Viewer window not open or unavailable";
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "open_graph") {
            int messageId = params["arguments"].toObject()["messageId"].toInt();
            QString signalName = params["arguments"].toObject()["signalName"].toString();
            
            QJsonObject resultData;
            if (MainWindow::getReference()->getGraphingWindow()) {
                MainWindow::getReference()->getGraphingWindow()->mcpOpenForSignal(messageId, signalName);
                resultData["success"] = true;
            } else {
                resultData["error"] = "Graphing window not open or unavailable";
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
