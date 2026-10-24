const { app, BrowserWindow, dialog, ipcMain, shell } = require('electron');
const path = require('node:path');
const fs = require('node:fs');

app.setName('GBX Studio');
if (process.platform === 'win32') app.setAppUserModelId('de.gbxstudio.ide');
process.env.GBXIDE_HOME = app.getPath('userData');

// Fenster-/Taskleisten-Icon (im gepackten Build steckt es in der EXE).
const iconPath = path.join(__dirname, '..', '..', 'assets', 'icon.ico');
const windowIcon = fs.existsSync(iconPath) ? iconPath : undefined;

let mainWindow = null;
let serverHandle = null;
// Erst nach der Rückfrage im Fenster („Speichern/Verwerfen“) darf es zugehen.
let allowClose = false;

const gotLock = app.requestSingleInstanceLock();
if (!gotLock) {
  app.quit();
} else {
  app.on('second-instance', () => {
    if (mainWindow) {
      if (mainWindow.isMinimized()) mainWindow.restore();
      mainWindow.focus();
    }
  });
}

function registerIpc() {
  ipcMain.handle('dialog:openFolder', async () => {
    const result = await dialog.showOpenDialog(mainWindow, {
      title: 'Projektordner öffnen',
      properties: ['openDirectory']
    });
    return result.canceled ? null : result.filePaths[0];
  });

  ipcMain.handle('dialog:openProject', async () => {
    const result = await dialog.showOpenDialog(mainWindow, {
      title: 'GBX-Projekt öffnen',
      properties: ['openFile', 'openDirectory'],
      filters: [
        { name: 'GBX-Projekt', extensions: ['gbxproj'] },
        { name: 'GBX-Quelle', extensions: ['gbx'] },
        { name: 'Alle Dateien', extensions: ['*'] }
      ]
    });
    return result.canceled ? null : result.filePaths[0];
  });

  ipcMain.handle('dialog:chooseLocation', async () => {
    const result = await dialog.showOpenDialog(mainWindow, {
      title: 'Speicherort für das Projekt wählen',
      properties: ['openDirectory', 'createDirectory']
    });
    return result.canceled ? null : result.filePaths[0];
  });

  ipcMain.handle('dialog:openFile', async (_event, options = {}) => {
    const result = await dialog.showOpenDialog(mainWindow, {
      title: options.title || 'Datei öffnen',
      defaultPath: options.defaultPath,
      properties: ['openFile'],
      filters: options.filters || [{ name: 'Alle Dateien', extensions: ['*'] }]
    });
    return result.canceled ? null : result.filePaths[0];
  });

  ipcMain.handle('dialog:saveFile', async (_event, options = {}) => {
    const result = await dialog.showSaveDialog(mainWindow, {
      title: options.title || 'Speichern unter',
      defaultPath: options.defaultPath,
      filters: options.filters || [{ name: 'GBX-Quelle', extensions: ['gbx'] }]
    });
    return result.canceled ? null : result.filePath;
  });

  ipcMain.handle('shell:reveal', async (_event, filePath) => {
    if (!filePath) return false;
    try {
      shell.showItemInFolder(filePath);
      return true;
    } catch {
      return false;
    }
  });

  // Ordner im Explorer öffnen; bei Dateien den Fundort markieren.
  ipcMain.handle('shell:openInExplorer', async (_event, target) => {
    if (!target) return false;
    try {
      const stat = fs.statSync(target);
      if (stat.isDirectory()) {
        const message = await shell.openPath(target);
        return !message;
      }
      shell.showItemInFolder(target);
      return true;
    } catch {
      return false;
    }
  });

  ipcMain.on('app:close-confirmed', () => {
    allowClose = true;
    if (mainWindow) mainWindow.close();
  });

  ipcMain.handle('app:info', () => ({
    version: app.getVersion(),
    platform: process.platform,
    userData: app.getPath('userData'),
    electron: process.versions.electron
  }));
}

async function boot() {
  const { startServer } = await import('../server/server.mjs');
  serverHandle = await startServer({ port: 0 });
  registerIpc();

  mainWindow = new BrowserWindow({
    width: 1480,
    height: 940,
    minWidth: 980,
    minHeight: 640,
    backgroundColor: '#14171c',
    title: 'GBX Studio',
    icon: windowIcon,
    autoHideMenuBar: true,
    show: false,
    webPreferences: {
      preload: path.join(__dirname, 'preload.cjs'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false,
      spellcheck: false
    }
  });

  // Das X im Fensterrahmen fragt zuerst im Fenster nach ungespeicherten
  // Änderungen; erst app:close-confirmed aus dem Renderer schließt wirklich.
  mainWindow.on('close', (event) => {
    if (allowClose) return;
    if (!mainWindow || mainWindow.webContents.isDestroyed()) {
      allowClose = true;
      mainWindow?.close();
      return;
    }
    event.preventDefault();
    mainWindow.webContents.send('app:close-request');
  });

  mainWindow.once('ready-to-show', () => mainWindow.show());
  mainWindow.webContents.setWindowOpenHandler(({ url }) => {
    if (/^https?:/i.test(url)) shell.openExternal(url);
    return { action: 'deny' };
  });

  await mainWindow.loadURL(serverHandle.url);
  if (process.argv.includes('--devtools')) mainWindow.webContents.openDevTools({ mode: 'detach' });

  const shotArg = process.argv.find((a) => a.startsWith('--screenshot='));
  if (shotArg) {
    const target = shotArg.slice('--screenshot='.length);
    setTimeout(async () => {
      try {
        const image = await mainWindow.webContents.capturePage();
        fs.writeFileSync(path.resolve(target), image.toPNG());
        console.log(`Screenshot gespeichert: ${target}`);
      } catch (error) {
        console.error(error);
      }
    }, 2500);
  }
}

app.whenReady().then(boot).catch((error) => {
  dialog.showErrorBox('GBX Studio', `Startfehler:\n${error?.stack || error}`);
  app.quit();
});

app.on('activate', () => {
  if (BrowserWindow.getAllWindows().length === 0) boot();
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit();
});

app.on('before-quit', () => {
  if (serverHandle) {
    serverHandle.close().catch(() => {});
    serverHandle = null;
  }
});
