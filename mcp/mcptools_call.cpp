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

static QJsonObject treeItemToJson(QTreeWidgetItem* item, bool excludeHistograms = true)
{
    QJsonObject obj;
    obj["text"] = item->text(0);
    if (item->childCount() > 0) {
        QJsonArray children;
        for( int i = 0; i < item->childCount(); ++i ) {
            QTreeWidgetItem* child = item->child(i);
            if (excludeHistograms) {
                QString text = child->text(0);
                if (text == "Histogram" || text == "Bitfield Histogram" || text == "Bitchange Heatmap") {
                    continue;
                }
            }
            children.append(treeItemToJson(child, excludeHistograms));
        }
        if (!children.isEmpty()) {
            obj["children"] = children;
        }
    }
    return obj;
}

void MCPServer::handleToolsCall(const QJsonObject &request, QJsonObject &response)
{
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
            if (!frameId.startsWith("0x", Qt::CaseInsensitive) && !frameId.startsWith("x", Qt::CaseInsensitive)) {
                frameId = "0x" + frameId;
            }
            
            uint32_t idInt = Utility::ParseStringToNum(frameId);
            QString standardId = Utility::formatCANID(idInt);
            
            // This runs on the main GUI thread, so it's safe to manipulate the UI
            MainWindow::getReference()->analyzeFrameData(standardId);
            
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
                QJsonArray statsArr;
                QTreeWidget* tree = fiw->getDetailsTree();
                if (tree) {
                    QTreeWidgetItem *root = tree->invisibleRootItem();
                    for (int i = 0; i < root->childCount(); ++i) {
                        QTreeWidgetItem* child = root->child(i);
                        QString text = child->text(0);
                        if (text == "Histogram" || text == "Bitfield Histogram" || text == "Bitchange Heatmap") {
                            continue;
                        }
                        statsArr.append(treeItemToJson(child, true));
                    }
                }
                QJsonObject statsItem;
                statsItem["type"] = "text";
                statsItem["text"] = "And here are the computation statistics computed by the application:\n" + QString::fromUtf8(QJsonDocument(statsArr).toJson(QJsonDocument::Indented));
                content.append(statsItem);
            }
            
        } else if (toolName == "query_can_logs") {
            QJsonObject args = params["arguments"].toObject();
            
            bool filterId = args.contains("frameId");
            uint32_t targetId = 0;
            if (filterId) {
                QString fId = args["frameId"].toString();
                if (!fId.startsWith("0x", Qt::CaseInsensitive) && !fId.startsWith("x", Qt::CaseInsensitive)) {
                    fId = "0x" + fId;
                }
                targetId = Utility::ParseStringToNum(fId);
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
                if (!MainWindow::getReference()->getSnifferWindow()) {
                     MainWindow::getReference()->showSnifferWindow();
                }
                if (MainWindow::getReference()->getSnifferWindow()) {
                     const SnifferModel* model = MainWindow::getReference()->getSnifferWindow()->getModel();
                     if (model) {
                         QJsonObject result;
                         QJsonArray items;
                         const QMap<unsigned int, SnifferItem*>& mMap = model->getMap();
                         for (auto it = mMap.constBegin(); it != mMap.constEnd(); ++it) {
                             SnifferItem *sniff = it.value();
                             QJsonObject obj;
                             obj["id"] = QString::number(sniff->getId(), 16).toUpper();
                             obj["delta"] = sniff->getDelta();
                             
                             QJsonArray dataArr;
                             for (int i=0; i<8; i++) {
                                  QJsonObject dObj;
                                  dObj["value"] = sniff->getData(i);
                                  dObj["changed"] = (sniff->dataChange(i) != NO);
                                  dataArr.append(dObj);
                             }
                             obj["data"] = dataArr;
                             items.append(obj);
                         }
                         result["tracked_ids"] = items;
                         result["active_count"] = mMap.size();
                         resultData = result;
                     }
                } else {
                     resultData["error"] = "Sniffer window could not be opened";
                }
            } else if (target == "flowview") {
                if (!MainWindow::getReference()->getFlowViewWindow()) {
                     MainWindow::getReference()->showFlowViewWindow();
                }
                if (MainWindow::getReference()->getFlowViewWindow()) {
                     QJsonObject obj;
                     obj["selected_id"] = QString::number(MainWindow::getReference()->getFlowViewWindow()->getSelectedId(), 16).toUpper();
                     obj["frame_count"] = MainWindow::getReference()->getFlowViewWindow()->getFrameCount();
                     QJsonArray triggers;
                     for (int i=0; i<8; i++) {
                         int val = MainWindow::getReference()->getFlowViewWindow()->getTriggerValue(i);
                         if (val != -1) {
                              QJsonObject t;
                              t["byte"] = i;
                              t["value"] = val;
                              triggers.append(t);
                         }
                     }
                     obj["triggers"] = triggers;
                     resultData = obj;
                } else {
                     resultData["error"] = "Flow View window could not be opened";
                }
            } else if (target == "bisect") {
                if (!MainWindow::getReference()->getBisectWindow()) {
                     MainWindow::getReference()->showBisectWindow();
                }
                if (MainWindow::getReference()->getBisectWindow()) {
                     QJsonObject obj;
                     obj["main_frame_count"] = MainWindow::getReference()->getBisectWindow()->getMainFrameCount();
                     obj["split_frame_count"] = MainWindow::getReference()->getBisectWindow()->getSplitFrameCount();
                     resultData = obj;
                } else {
                     resultData["error"] = "Bisect window could not be opened";
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
            if (!MainWindow::getReference()->getFuzzingWindow()) {
                MainWindow::getReference()->showFuzzingWindow();
            }
            if (MainWindow::getReference()->getFuzzingWindow()) {
                MainWindow::getReference()->getFuzzingWindow()->configureFuzzer(startId, endId, intervalMs, fuzzType);
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
            if (!MainWindow::getReference()->getFuzzingWindow()) {
                MainWindow::getReference()->showFuzzingWindow();
            }
            if (MainWindow::getReference()->getFuzzingWindow()) {
                MainWindow::getReference()->getFuzzingWindow()->startFuzzing();
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
            if (!MainWindow::getReference()->getFuzzingWindow()) {
                MainWindow::getReference()->showFuzzingWindow();
            }
            if (MainWindow::getReference()->getFuzzingWindow()) {
                MainWindow::getReference()->getFuzzingWindow()->stopFuzzing();
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
            if (!MainWindow::getReference()->getUDSScanWindow()) {
                MainWindow::getReference()->showUDSScanWindow();
            }
            if (MainWindow::getReference()->getUDSScanWindow()) {
                MainWindow::getReference()->getUDSScanWindow()->openAndConfigure(startId, endId, bus, scanType);
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
            if (!MainWindow::getReference()->getUDSScanWindow()) {
                MainWindow::getReference()->showUDSScanWindow();
            }
            if (MainWindow::getReference()->getUDSScanWindow()) {
                MainWindow::getReference()->getUDSScanWindow()->startFuzzingScan();
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
            if (!MainWindow::getReference()->getUDSScanWindow()) {
                MainWindow::getReference()->showUDSScanWindow();
            }
            if (MainWindow::getReference()->getUDSScanWindow()) {
                MainWindow::getReference()->getUDSScanWindow()->stopFuzzingScan();
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
            if (!MainWindow::getReference()->getISOTPWindow()) {
                MainWindow::getReference()->showISOInterpreterWindow();
            }
            if (MainWindow::getReference()->getISOTPWindow()) {
                MainWindow::getReference()->getISOTPWindow()->openAndConfigure(rxId);
                resultData["success"] = true;
            } else {
                resultData["error"] = "ISO-TP window not open or unavailable";
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "query_isotp_messages") {
            int limit = 50;
            if (params["arguments"].toObject().contains("limit")) {
                limit = params["arguments"].toObject()["limit"].toInt();
            }
            
            QJsonObject resultData;
            if (!MainWindow::getReference()->getISOTPWindow()) {
                MainWindow::getReference()->showISOInterpreterWindow();
            }
            if (MainWindow::getReference()->getISOTPWindow()) {
                const QVector<ISOTP_MESSAGE>& messages = MainWindow::getReference()->getISOTPWindow()->getMessagesList();
                int count = 0;
                QJsonArray resultArr;
                for (int i = messages.size() - 1; i >= 0 && count < limit; --i) {
                    ISOTP_MESSAGE msg = messages.at(i);
                    QJsonObject msgObj;
                    msgObj["timestamp"] = (qint64)msg.timeStamp().microSeconds();
                    msgObj["id"] = (int)msg.frameId();
                    msgObj["bus"] = msg.bus;
                    msgObj["length"] = msg.reportedLength;
                    msgObj["data_hex"] = QString(msg.payload().toHex());
                    msgObj["is_rx"] = msg.isReceived;
                    resultArr.prepend(msgObj);
                    count++;
                }
                resultData["messages"] = resultArr;
                resultData["success"] = true;
            } else {
                resultData["error"] = "ISO-TP window not open or unavailable";
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "send_isotp_message") {
            int bus = params["arguments"].toObject()["bus"].toInt();
            int id = params["arguments"].toObject()["id"].toInt();
            QString dataStr = params["arguments"].toObject()["data"].toString();
            QByteArray data = QByteArray::fromHex(dataStr.toUtf8());
            
            QJsonObject resultData;
            if (!MainWindow::getReference()->getISOTPWindow()) {
                MainWindow::getReference()->showISOInterpreterWindow();
            }
            if (MainWindow::getReference()->getISOTPWindow()) {
                MainWindow::getReference()->getISOTPWindow()->sendISOTPFrame(bus, id, data);
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
            if (!MainWindow::getReference()->getFrameSenderWindow()) {
                MainWindow::getReference()->showFrameSenderWindow();
            }
            if (MainWindow::getReference()->getFrameSenderWindow()) {
                MainWindow::getReference()->getFrameSenderWindow()->addSequence(bus, id, data, intervalMs);
                resultData["success"] = true;
            } else {
                resultData["error"] = "Frame Sender window not open or unavailable";
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "start_frame_sender") {
            QJsonObject resultData;
            if (!MainWindow::getReference()->getFrameSenderWindow()) {
                MainWindow::getReference()->showFrameSenderWindow();
            }
            if (MainWindow::getReference()->getFrameSenderWindow()) {
                MainWindow::getReference()->getFrameSenderWindow()->startAll();
                resultData["success"] = true;
            } else {
                resultData["error"] = "Frame Sender window not open or unavailable";
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "stop_frame_sender") {
            QJsonObject resultData;
            if (!MainWindow::getReference()->getFrameSenderWindow()) {
                MainWindow::getReference()->showFrameSenderWindow();
            }
            if (MainWindow::getReference()->getFrameSenderWindow()) {
                MainWindow::getReference()->getFrameSenderWindow()->stopAll();
                resultData["success"] = true;
            } else {
                resultData["error"] = "Frame Sender window not open or unavailable";
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "clear_frame_sender") {
            QJsonObject resultData;
            if (!MainWindow::getReference()->getFrameSenderWindow()) {
                MainWindow::getReference()->showFrameSenderWindow();
            }
            if (MainWindow::getReference()->getFrameSenderWindow()) {
                MainWindow::getReference()->getFrameSenderWindow()->clearAll();
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
            if (!MainWindow::getReference()->getSignalViewerWindow()) {
                MainWindow::getReference()->showSignalViewer();
            }
            if (MainWindow::getReference()->getSignalViewerWindow()) {
                MainWindow::getReference()->getSignalViewerWindow()->openForSignal(messageId, signalName);
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
            if (!MainWindow::getReference()->getGraphingWindow()) {
                MainWindow::getReference()->showGraphingWindow();
            }
            if (MainWindow::getReference()->getGraphingWindow()) {
                MainWindow::getReference()->getGraphingWindow()->openForSignal(messageId, signalName);
                resultData["success"] = true;
            } else {
                resultData["error"] = "Graphing window not open or unavailable";
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "query_graph_data") {
            int graphIdx = 0;
            int limit = 100;
            if (params["arguments"].toObject().contains("graphIdx")) {
                graphIdx = params["arguments"].toObject()["graphIdx"].toInt();
            }
            if (params["arguments"].toObject().contains("limit")) {
                limit = params["arguments"].toObject()["limit"].toInt();
            }
            
            QJsonObject resultData;
            if (!MainWindow::getReference()->getGraphingWindow()) {
                MainWindow::getReference()->showGraphingWindow();
            }
            if (MainWindow::getReference()->getGraphingWindow()) {
                const QList<GraphParams>& paramsList = MainWindow::getReference()->getGraphingWindow()->getGraphParamsList();
                if (graphIdx >= 0 && graphIdx < paramsList.count()) {
                    const GraphParams &gp = paramsList[graphIdx];
                    int count = 0;
                    QJsonArray data;
                    for (int i = gp.x.size() - 1; i >= 0 && count < limit; --i) {
                        QJsonObject pt;
                        pt["x"] = gp.x.at(i);
                        pt["y"] = gp.y.at(i);
                        data.prepend(pt);
                        count++;
                    }
                    resultData["data"] = data;
                    resultData["success"] = true;
                } else {
                    resultData["error"] = "Invalid graph index";
                }
            } else {
                resultData["error"] = "Graphing window not open or unavailable";
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "take_graph_screenshot") {
            QString filepath = params["arguments"].toObject()["filepath"].toString();
            
            QJsonObject resultData;
            if (!MainWindow::getReference()->getGraphingWindow()) {
                MainWindow::getReference()->showGraphingWindow();
            }
            if (MainWindow::getReference()->getGraphingWindow()) {
                QString res = MainWindow::getReference()->getGraphingWindow()->takeScreenshot(filepath);
                if (!res.isEmpty()) {
                    resultData["filepath"] = res;
                    resultData["success"] = true;
                } else {
                    resultData["error"] = "Failed to take screenshot";
                }
            } else {
                resultData["error"] = "Graphing window not open or unavailable";
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "start_playback") {
            QJsonObject resultData;
            if (!MainWindow::getReference()->getPlaybackWindow()) {
                MainWindow::getReference()->showPlaybackWindow();
            }
            if (MainWindow::getReference()->getPlaybackWindow()) {
                MainWindow::getReference()->getPlaybackWindow()->playPlayback();
                resultData["success"] = true;
            } else {
                resultData["error"] = "Playback window not open or unavailable";
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "pause_playback") {
            QJsonObject resultData;
            if (!MainWindow::getReference()->getPlaybackWindow()) {
                MainWindow::getReference()->showPlaybackWindow();
            }
            if (MainWindow::getReference()->getPlaybackWindow()) {
                MainWindow::getReference()->getPlaybackWindow()->pausePlayback();
                resultData["success"] = true;
            } else {
                resultData["error"] = "Playback window not open or unavailable";
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "stop_playback") {
            QJsonObject resultData;
            if (!MainWindow::getReference()->getPlaybackWindow()) {
                MainWindow::getReference()->showPlaybackWindow();
            }
            if (MainWindow::getReference()->getPlaybackWindow()) {
                MainWindow::getReference()->getPlaybackWindow()->stopPlayback();
                resultData["success"] = true;
            } else {
                resultData["error"] = "Playback window not open or unavailable";
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "connect_bus") {
            int type = params["arguments"].toObject().contains("type") ? params["arguments"].toObject()["type"].toInt() : 0;
            QString portName = params["arguments"].toObject().contains("portName") ? params["arguments"].toObject()["portName"].toString() : "";
            QString driverName = params["arguments"].toObject().contains("driverName") ? params["arguments"].toObject()["driverName"].toString() : "";
            int serialSpeed = params["arguments"].toObject().contains("serialSpeed") ? params["arguments"].toObject()["serialSpeed"].toInt() : 0;
            int busSpeed = params["arguments"].toObject().contains("busSpeed") ? params["arguments"].toObject()["busSpeed"].toInt() : 0;
            bool isCanFd = params["arguments"].toObject().contains("isCanFd") ? params["arguments"].toObject()["isCanFd"].toBool() : false;
            int dataRate = params["arguments"].toObject().contains("dataRate") ? params["arguments"].toObject()["dataRate"].toInt() : 0;
            
            QJsonObject resultData;
            if (MainWindow::getReference()->getConnectionWindow()) {
                if (MainWindow::getReference()->getConnectionWindow()->connectBus(type, portName, driverName, serialSpeed, busSpeed, isCanFd, dataRate)) {
                    resultData["success"] = true;
                } else {
                    resultData["error"] = "Failed to create connection";
                }
            } else {
                resultData["error"] = "Connection window unavailable";
            }
            
            QJsonObject item;
            item["type"] = "text";
            item["text"] = QString::fromUtf8(QJsonDocument(resultData).toJson(QJsonDocument::Indented));
            content.append(item);
            
        } else if (toolName == "disconnect_bus") {
            int index = params["arguments"].toObject()["index"].toInt();
            
            QJsonObject resultData;
            if (MainWindow::getReference()->getConnectionWindow()) {
                if (MainWindow::getReference()->getConnectionWindow()->disconnectBus(index)) {
                    resultData["success"] = true;
                } else {
                    resultData["error"] = "Failed to disconnect, invalid index?";
                }
            } else {
                resultData["error"] = "Connection window unavailable";
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
