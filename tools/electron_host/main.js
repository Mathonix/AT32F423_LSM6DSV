const { app, BrowserWindow, ipcMain } = require('electron');
const path = require('node:path');
const { SerialPort } = require('serialport');

let win;
let port;

function createWindow() {
  win = new BrowserWindow({
    width: 1420,
    height: 920,
    minWidth: 1050,
    minHeight: 700,
    backgroundColor: '#08111f',
    webPreferences: { preload: path.join(__dirname, 'preload.js'), contextIsolation: true, nodeIntegration: false }
  });
  win.webContents.on('render-process-gone', (_event, details) => {
    console.error('renderer process exited:', details.reason, details.exitCode);
  });
  win.webContents.on('console-message', (_event, _level, message) => {
    console.log('[renderer]', message);
  });
  win.loadFile(path.join(__dirname, 'index.html'));
}

function send(event, type, data) {
  if (!win || win.isDestroyed()) return;
  win.webContents.send(type, data);
}

ipcMain.handle('ports:list', async () => {
  const ports = await SerialPort.list();
  return ports.map((p) => ({ path: p.path, manufacturer: p.manufacturer || '', friendlyName: p.friendlyName || '', serialNumber: p.serialNumber || '', vendorId: p.vendorId || '', productId: p.productId || '' }));
});

ipcMain.handle('serial:open', async (_event, options) => {
  if (port?.isOpen) await new Promise((resolve) => port.close(() => resolve()));
  port = new SerialPort({ path: options.path, baudRate: Number(options.baudRate) || 2000000, autoOpen: false });
  port.on('data', (data) => send(null, 'serial:data', Array.from(data)));
  port.on('error', (error) => send(null, 'serial:error', error.message));
  port.on('close', () => send(null, 'serial:closed'));
  await new Promise((resolve, reject) => port.open((error) => error ? reject(error) : resolve()));
  return { path: options.path };
});

ipcMain.handle('serial:write', async (_event, bytes) => {
  if (!port?.isOpen) throw new Error('串口未连接');
  const data = Buffer.from(bytes);
  await new Promise((resolve, reject) => port.write(data, (error) => error ? reject(error) : port.drain((drainError) => drainError ? reject(drainError) : resolve())));
  return true;
});

ipcMain.handle('serial:close', async () => {
  if (port?.isOpen) await new Promise((resolve) => port.close(() => resolve()));
  port = null;
  return true;
});

app.whenReady().then(() => {
  createWindow();
  app.on('activate', () => { if (BrowserWindow.getAllWindows().length === 0) createWindow(); });
});
app.on('window-all-closed', () => { if (process.platform !== 'darwin') app.quit(); });
