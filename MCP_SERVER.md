# SavvyLens AI Co-pilot (MCP Server)

SavvyLens includes a built-in [Model Context Protocol (MCP)](https://modelcontextprotocol.io/) server. This allows AI assistants and agents to connect directly to your SavvyLens application and help you analyze CAN frames, reverse-engineer signals, read your DBC files, and even control the application.

## How it works
The MCP server runs as a TCP server on port `8888` by default. You can configure this in the `Settings -> AI Co-pilot (MCP) Settings` dialog. 

Since most AI MCP clients only support standard I/O (stdio) communication rather than direct TCP, a Python bridge script (`mcp_bridge.py`) is provided. This bridge runs locally in your AI environment and forwards the AI's stdio requests to the SavvyLens TCP server.

## Setting up your AI Client

1. **Prerequisites**: Ensure you have **Python 3** installed on your system.
2. **Download the Bridge**: Make sure you have the `mcp_bridge.py` script downloaded on your computer.
3. **Configure your AI tool** (e.g., Claude Desktop, Cline):
   You will need to configure your AI tool to launch the bridge as a new MCP server. Your configuration will typically look like this:

   ```json
   {
     "mcpServers": {
       "savvylens-mcp": {
         "command": "python3",
         "args": [
           "/absolute/path/to/your/mcp_bridge.py"
         ]
       }
     }
   }
   ```
4. **Enable in SavvyLens**: Open SavvyLens, go to `Settings -> AI Co-pilot (MCP) Settings` and check `Enable MCP TCP Server`. The default port is `8888`.
5. **Connect!**: When the AI client starts, it will run the python bridge, which will automatically connect to SavvyLens on port `8888`. You will see "AI: 1 Connected" in the bottom right status bar of SavvyLens!

## What can the AI do?
Once connected, the AI can:
- Query captured CAN logs and filter by timestamp, bus, or ID.
- Open analysis windows and retrieve computation statistics.
- Inject raw CAN frames and configure the Fuzzer.
- Read and manage DBC nodes, messages, and signals.
- Interact with UDS Scanners and ISO-TP interpreters.
- Monitor active connections and playback objects.
