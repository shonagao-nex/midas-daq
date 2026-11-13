import { settings } from 'jsroot/core';
import { TFramePainter } from 'jsrootsys/modules/gpad/TFramePainter.mjs';

// JSROOT omits axes below the SmallPad frame size. A 32-panel canvas can
// fall below the stock threshold even when its sparse labels still fit.
if (!new URLSearchParams(window.location.search).has('smallpad')) {
  settings.SmallPad = { width: 80, height: 60 };
}

// Set the initial vertical scale of the SciFi canvases.
const sciFiPages = new Set([
  'SciFi_1X_1Y_ADC', 'SciFi_1X_1Y_TDC_Leading',
  'SciFi_2X_2Y_ADC', 'SciFi_2X_2Y_TDC_Leading',
]);
const createXY = TFramePainter.prototype.createXY;
TFramePainter.prototype.createXY = function(opts) {
  const painter = this.getPadPainter();
  const pad = painter?.getRootPad();
  const canvas = painter?.getCanvPainter()?.getRootPad();
  if (opts?.ndim === 1 && pad && sciFiPages.has(canvas?.fName) && !painter._interactively_changed)
    pad.fLogy = 1;
  return createXY.call(this, opts);
};
