import socket
import sys
import os
import threading
import time
import json

sock = None
sock_lock = threading.Lock()

def get_socket():
    global sock
    with sock_lock:
        if sock is not None:
            return sock
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect(('localhost', 8888))
            sock = s
            return sock
        except:
            return None

def drop_socket():
    global sock
    with sock_lock:
        if sock is not None:
            try:
                sock.close()
            except:
                pass
            sock = None

def forward_sock_to_stdout():
    while True:
        s = get_socket()
        if s is None:
            time.sleep(1)
            continue
        try:
            data = s.recv(4096)
            if not data:
                drop_socket()
                continue
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()
        except Exception:
            drop_socket()
            time.sleep(1)

def forward_stdin_to_sock():
    global sock
    while True:
        try:
            data = os.read(0, 4096)
            if not data:
                break
            
            sent = False
            # Retry a few times in case the server is just restarting
            for _ in range(3):
                s = get_socket()
                if s:
                    try:
                        s.sendall(data)
                        sent = True
                        break
                    except Exception:
                        drop_socket()
                time.sleep(1)
                
            if not sent:
                # If we couldn't send, reply with a JSON-RPC error so the client doesn't hang
                lines = data.decode('utf-8', errors='ignore').split('\n')
                for line in lines:
                    line = line.strip()
                    if line:
                        try:
                            req = json.loads(line)
                            if "id" in req:
                                err = {
                                    "jsonrpc": "2.0",
                                    "id": req["id"],
                                    "error": {"code": -32000, "message": "SavvyLens is currently offline. Please start it."}
                                }
                                print(json.dumps(err), flush=True)
                        except:
                            pass
        except Exception:
            pass

def main():
    t = threading.Thread(target=forward_sock_to_stdout)
    t.daemon = True
    t.start()
    
    forward_stdin_to_sock()

if __name__ == '__main__':
    main()
