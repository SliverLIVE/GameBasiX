const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('gbxStudioDesktop', {
  isElectron: true,
  openFolder: () => ipcRenderer.invoke('dialog:openFolder'),
  openProject: () => ipcRenderer.invoke('dialog:openProject'),
  chooseLocation: () => ipcRenderer.invoke('dialog:chooseLocation'),
  openFile: (options) => ipcRenderer.invoke('dialog:openFile', options),
  saveFile: (options) => ipcRenderer.invoke('dialog:saveFile', options),
  reveal: (filePath) => ipcRenderer.invoke('shell:reveal', filePath),
  openInExplorer: (target) => ipcRenderer.invoke('shell:openInExplorer', target),
  info: () => ipcRenderer.invoke('app:info'),
  // Das Hauptfenster fragt beim Schließen (X) im Renderer nach.
  onCloseRequest: (callback) => {
    if (typeof callback !== 'function') return () => {};
    const handler = () => callback();
    ipcRenderer.on('app:close-request', handler);
    return () => ipcRenderer.removeListener('app:close-request', handler);
  },
  confirmClose: () => ipcRenderer.send('app:close-confirmed')
});
