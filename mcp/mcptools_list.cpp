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

void MCPServer::handleToolsList(QJsonObject &response)
{
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
        
        QJsonObject startFrameSenderTool;
        startFrameSenderTool["name"] = "start_frame_sender";
        startFrameSenderTool["description"] = "Enable and start sending all frame sequences in the Frame Sender.";
        startFrameSenderTool["inputSchema"] = QJsonObject({{"type", "object"}, {"properties", QJsonObject()}});
        
        QJsonObject stopFrameSenderTool;
        stopFrameSenderTool["name"] = "stop_frame_sender";
        stopFrameSenderTool["description"] = "Disable and stop sending all frame sequences in the Frame Sender.";
        stopFrameSenderTool["inputSchema"] = QJsonObject({{"type", "object"}, {"properties", QJsonObject()}});
        
        QJsonObject clearFrameSenderTool;
        clearFrameSenderTool["name"] = "clear_frame_sender";
        clearFrameSenderTool["description"] = "Clear all frame sequences in the Frame Sender.";
        clearFrameSenderTool["inputSchema"] = QJsonObject({{"type", "object"}, {"properties", QJsonObject()}});
        
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
        
        QJsonObject queryGraphDataTool;
        queryGraphDataTool["name"] = "query_graph_data";
        queryGraphDataTool["description"] = "Fetch data points for a specific graph in the Graphing window.";
        QJsonObject queryGraphDataSchema;
        queryGraphDataSchema["type"] = "object";
        QJsonObject queryGraphDataProps;
        queryGraphDataProps["graphIdx"] = QJsonObject({{"type", "integer"}, {"description", "Index of the graph (0 for first graph)"}});
        queryGraphDataProps["limit"] = QJsonObject({{"type", "integer"}, {"description", "Maximum number of points to return (default 100)"}});
        queryGraphDataSchema["properties"] = queryGraphDataProps;
        queryGraphDataTool["inputSchema"] = queryGraphDataSchema;
        
        QJsonObject takeGraphScreenshotTool;
        takeGraphScreenshotTool["name"] = "take_graph_screenshot";
        takeGraphScreenshotTool["description"] = "Take a screenshot of the Graphing window and save to disk.";
        QJsonObject takeGraphScreenshotSchema;
        takeGraphScreenshotSchema["type"] = "object";
        QJsonObject takeGraphScreenshotProps;
        takeGraphScreenshotProps["filepath"] = QJsonObject({{"type", "string"}, {"description", "Absolute path to save the image (e.g. /tmp/graph.png)"}});
        takeGraphScreenshotSchema["properties"] = takeGraphScreenshotProps;
        takeGraphScreenshotSchema["required"] = QJsonArray() << "filepath";
        takeGraphScreenshotTool["inputSchema"] = takeGraphScreenshotSchema;
        QJsonObject startPlaybackTool;
        startPlaybackTool["name"] = "start_playback";
        startPlaybackTool["description"] = "Start playback of loaded logs in the Frame Playback window.";
        startPlaybackTool["inputSchema"] = QJsonObject({{"type", "object"}, {"properties", QJsonObject()}});
        
        QJsonObject pausePlaybackTool;
        pausePlaybackTool["name"] = "pause_playback";
        pausePlaybackTool["description"] = "Pause playback of loaded logs in the Frame Playback window.";
        pausePlaybackTool["inputSchema"] = QJsonObject({{"type", "object"}, {"properties", QJsonObject()}});
        
        QJsonObject stopPlaybackTool;
        stopPlaybackTool["name"] = "stop_playback";
        stopPlaybackTool["description"] = "Stop and reset playback in the Frame Playback window.";
        stopPlaybackTool["inputSchema"] = QJsonObject({{"type", "object"}, {"properties", QJsonObject()}});
        
        QJsonObject connectBusTool;
        connectBusTool["name"] = "connect_bus";
        connectBusTool["description"] = "Connect a new CAN bus or device.";
        QJsonObject connectBusSchema;
        connectBusSchema["type"] = "object";
        QJsonObject connectBusProps;
        connectBusProps["type"] = QJsonObject({{"type", "integer"}, {"description", "Connection type enum (e.g. 0=GVRET, 1=KVASER, etc.)"}});
        connectBusProps["portName"] = QJsonObject({{"type", "string"}});
        connectBusProps["driverName"] = QJsonObject({{"type", "string"}});
        connectBusProps["serialSpeed"] = QJsonObject({{"type", "integer"}});
        connectBusProps["busSpeed"] = QJsonObject({{"type", "integer"}});
        connectBusProps["isCanFd"] = QJsonObject({{"type", "boolean"}});
        connectBusProps["dataRate"] = QJsonObject({{"type", "integer"}});
        connectBusSchema["properties"] = connectBusProps;
        connectBusTool["inputSchema"] = connectBusSchema;
        
        QJsonObject disconnectBusTool;
        disconnectBusTool["name"] = "disconnect_bus";
        disconnectBusTool["description"] = "Disconnect a CAN bus by index.";
        QJsonObject disconnectBusSchema;
        disconnectBusSchema["type"] = "object";
        QJsonObject disconnectBusProps;
        disconnectBusProps["index"] = QJsonObject({{"type", "integer"}});
        disconnectBusSchema["properties"] = disconnectBusProps;
        disconnectBusSchema["required"] = QJsonArray() << "index";
        disconnectBusTool["inputSchema"] = disconnectBusSchema;
        
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
        QJsonObject queryIsotpTool;
        queryIsotpTool["name"] = "query_isotp_messages";
        queryIsotpTool["description"] = "Fetch decoded ISO-TP messages from the Interpreter window.";
        QJsonObject queryIsotpSchema;
        queryIsotpSchema["type"] = "object";
        QJsonObject queryIsotpProps;
        queryIsotpProps["limit"] = QJsonObject({{"type", "integer"}, {"description", "Maximum number of messages to return (default 50)"}});
        queryIsotpSchema["properties"] = queryIsotpProps;
        queryIsotpTool["inputSchema"] = queryIsotpSchema;
        
        QJsonObject sendIsotpTool;
        sendIsotpTool["name"] = "send_isotp_message";
        sendIsotpTool["description"] = "Inject an ISO-TP message (handles segmentation automatically).";
        QJsonObject sendIsotpSchema;
        sendIsotpSchema["type"] = "object";
        QJsonObject sendIsotpProps;
        sendIsotpProps["bus"] = QJsonObject({{"type", "integer"}});
        sendIsotpProps["id"] = QJsonObject({{"type", "integer"}});
        sendIsotpProps["data"] = QJsonObject({{"type", "string"}, {"description", "Hex string of the full payload to send"}});
        sendIsotpSchema["properties"] = sendIsotpProps;
        sendIsotpSchema["required"] = QJsonArray() << "bus" << "id" << "data";
        sendIsotpTool["inputSchema"] = sendIsotpSchema;
        
        tools.append(openIsotpTool);
        tools.append(queryIsotpTool);
        tools.append(sendIsotpTool);
        tools.append(addFrameSenderTool);
        tools.append(startFrameSenderTool);
        tools.append(stopFrameSenderTool);
        tools.append(clearFrameSenderTool);
        tools.append(openSignalViewerTool);
        tools.append(openGraphTool);
        tools.append(queryGraphDataTool);
        tools.append(takeGraphScreenshotTool);
        tools.append(startPlaybackTool);
        tools.append(pausePlaybackTool);
        tools.append(stopPlaybackTool);
        tools.append(connectBusTool);
        tools.append(disconnectBusTool);
        result["tools"] = tools;
        response["result"] = result;
}
