const protocol = require('./protocol');
﻿const { contextBridge, ipcRenderer } = require('electron');
contextBridge.exposeInMainWorld('host', {
  listPorts: () => ipcRenderer.invoke('ports:list'),
  open: (options) => ipcRenderer.invoke('serial:open', options),
  write: (bytes) => ipcRenderer.invoke('serial:write', bytes),
  close: () => ipcRenderer.invoke('serial:close'),
  onData: (callback) => ipcRenderer.on('serial:data', (_event, data) => callback(data)),
  onError: (callback) => ipcRenderer.on('serial:error', (_event, message) => callback(message)),
  onClosed: (callback) => ipcRenderer.on('serial:closed', callback),
  protocol: { CMD: protocol.CMD, MODE: protocol.MODE, ACK: protocol.ACK, packet: protocol.packet, parseBinary: protocol.parseBinary, decodeBinary: protocol.decodeBinary, parseJustFloat: protocol.parseJustFloat }
});
