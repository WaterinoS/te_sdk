# te_sdk

📦 `te_sdk` is a lightweight C++ SDK library designed to help developers build their own mods and tools using RakNet networking.

---

## ✨ Currently Supported Features

✅ **RakNet Hooking**  
You can hook the following RakNet events:

- **Incoming RPC** (`onIncomingRpc`)
- **Incoming Packet** (`onIncomingPacket`)
- **Outgoing RPC** (`onOutgoingRpc`)
- **Outgoing Packet** (`onOutgoingPacket`)

---

## 🔄 Compatibility

- Works with **any SA-MP version**
- No dependency on a specific game build or RakNet variant

---

## ⚠️ Note

This SDK is still under development. Future versions may introduce:

- Additional hook points
- Built-in packet/RPC decoding
- Game engine-specific extensions

---

## 🔧 Usage

1. Link `te_sdk.lib` in your project (DLL/EXE).
2. Include the public API from `te_sdk.h`.
3. Call `InitRakNetHooks(...)` and register your callbacks.

> A complete usage example will be added soon.

---

## 📄 License

This project is released as **freeware**.  
You are allowed to **use it in binary form only** (e.g., linking `.lib` into your project).  
**Modifying, redistributing, or publishing the source code is not permitted.**

---
