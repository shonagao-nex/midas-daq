import { settings } from 'jsroot/core';

// JSROOT omits axes below the SmallPad frame size. A 32-panel canvas can
// fall below the stock threshold even when its sparse labels still fit.
if (!new URLSearchParams(window.location.search).has('smallpad')) {
  settings.SmallPad = { width: 80, height: 60 };
}
