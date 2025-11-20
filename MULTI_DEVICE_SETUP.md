# Multi-Device Setup Guide

This guide helps you set up the NFS system across multiple devices (different computers/networks).

## Problem Fixed ✅

**Issue:** Storage Server (SS) was hardcoded to connect to `127.0.0.1` (localhost), preventing it from connecting to Name Server (NM) on a different device.

**Solution:** SS now accepts NM IP address as a command-line argument.

## Architecture Overview

```
┌─────────────────┐         ┌─────────────────┐         ┌─────────────────┐
│   Device 1      │         │   Device 2      │         │   Device 3      │
│                 │         │                 │         │                 │
│  Name Server    │◄────────┤ Storage Server  │         │    Client       │
│  (NM)           │         │  (SS)           │         │                 │
│  Port: 8080     │         │  Port: 9001     │         │                 │
└─────────────────┘         └─────────────────┘         └─────────────────┘
      ▲                                                          │
      └──────────────────────────────────────────────────────────┘
```

## Setup Steps

### 1. Find IP Addresses

On each device, find its IP address:

```bash
# Linux/Mac
ip addr show    # or ifconfig
hostname -I

# Windows
ipconfig
```

Example IPs used in this guide:
- Device 1 (NM): `192.168.1.100`
- Device 2 (SS): `192.168.1.101`
- Device 3 (Client): `192.168.1.102`

### 2. Start Name Server (Device 1)

```bash
cd NM
./name_server
```

The NM will listen on port `8080` on all network interfaces.

### 3. Start Storage Server (Device 2)

**NEW:** Pass the NM's IP address as a second argument:

```bash
cd SS
./storage_server 9001 192.168.1.100
```

Format: `./storage_server <port> <nm_ip>`
- `<port>`: Port for this storage server (e.g., 9001)
- `<nm_ip>`: IP address of the Name Server (e.g., 192.168.1.100)

**Note:** If NM is on the same device, you can omit the IP (defaults to 127.0.0.1):
```bash
./storage_server 9001
```

### 4. Start Client (Device 3)

```bash
cd client
./client 192.168.1.100
```

## Troubleshooting

### SS Cannot Connect to NM

**Symptom:** `Connection to NM Failed`

**Solutions:**
1. **Check NM IP:** Make sure you're using the correct IP address
   ```bash
   # On NM device, verify IP
   hostname -I
   ```

2. **Check Firewall:** Ensure port 8080 is open on NM device
   ```bash
   # Linux - check if port is listening
   sudo netstat -tlnp | grep 8080
   
   # Allow port through firewall
   sudo ufw allow 8080/tcp
   ```

3. **Test Connection:** Ping the NM device
   ```bash
   ping 192.168.1.100
   ```

4. **Check NM is Running:** On NM device:
   ```bash
   ps aux | grep name_server
   ```

### File Writes Not Working

**Symptom:** Files create but writes fail

**Solutions:**
1. **Check Permissions:** Ensure storage directory is writable
   ```bash
   # On SS device
   ls -la storage_root_9001/
   chmod 755 storage_root_9001/
   ```

2. **Check Logs:** Look at SS logs for detailed errors
   ```bash
   # On SS device
   tail -f storage_server.log
   ```

3. **Verify NM Connection:** SS needs to connect to NM for permission checks
   - Make sure SS connected successfully at startup
   - Check NM logs to see if SS registered

### Client Cannot See Files

**Symptom:** `LIST` shows no files or old files

**Solutions:**
1. **Restart SS:** After creating files, restart SS to re-register them
   ```bash
   # Kill SS
   pkill storage_server
   
   # Restart with NM IP
   ./storage_server 9001 192.168.1.100
   ```

2. **Check NM Registration:** NM should show SS registration in logs

## Testing Multi-Device Setup

### Quick Test Procedure

1. **On Client (Device 3):**
   ```
   nfs:user1> CREATE testfile
   nfs:user1> WRITE testfile 0
   Enter words> 0 Hello from remote device!
   Enter words> ETIRW
   nfs:user1> READ testfile
   ```

2. **Expected Output:**
   - File creates successfully
   - Write operation completes with `ACK:WRITE_COMPLETE`
   - Read returns: `Hello from remote device!`

3. **Check Logs:**
   - NM logs should show CREATE and access checks
   - SS logs should show file operations with IP addresses

## Common Configurations

### Configuration 1: All on Same Device (Development)
```bash
# Terminal 1
cd NM && ./name_server

# Terminal 2
cd SS && ./storage_server 9001

# Terminal 3
cd client && ./client
```

### Configuration 2: NM + SS on Server, Clients Remote
```bash
# Server (192.168.1.100)
cd NM && ./name_server
cd SS && ./storage_server 9001

# Client devices
./client 192.168.1.100
```

### Configuration 3: Separate Devices (Production)
```bash
# Device 1 - NM (192.168.1.100)
cd NM && ./name_server

# Device 2 - SS (192.168.1.101)
cd SS && ./storage_server 9001 192.168.1.100

# Device 3+ - Clients
./client 192.168.1.100
```

## Network Requirements

- **Ports to Open:**
  - Port 8080 (NM)
  - Port 9001+ (SS instances)
  
- **Firewall Rules:**
  - Allow incoming TCP on ports 8080, 9001
  - Allow established connections

- **Network Type:**
  - Local network (LAN): Works out of the box
  - Different networks: May need port forwarding/VPN

## Debugging Commands

```bash
# Check if NM is reachable from SS device
telnet 192.168.1.100 8080

# Check if SS registered files
# In NM logs, look for: "Registering file from SS"

# Verify SS can create files
ls -la SS/storage_root_9001/

# Check permissions
ls -l SS/storage_root_9001/testfile
```

## Key Changes Made

1. **SS now accepts NM IP:** `./storage_server <port> <nm_ip>`
2. **Better error logging:** File write errors now show errno details
3. **Dynamic NM IP:** SS uses provided IP instead of hardcoded 127.0.0.1

## Support

If issues persist:
1. Check all logs: `NM/nm_server.log`, `SS/storage_server.log`
2. Verify network connectivity with `ping` and `telnet`
3. Ensure all devices have correct firewall rules
4. Try same-device setup first, then expand to multiple devices
