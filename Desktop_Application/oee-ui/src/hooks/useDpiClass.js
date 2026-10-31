import { useEffect } from "react";

/**
 * useDpiClass
 * -----------
 * Reads window.devicePixelRatio (= Windows display-scale factor):
 *   1.0  → 100% scale → 96 DPI  → adds class "dpi-96"
 *   1.25 → 125% scale → 120 DPI → adds class "dpi-120"
 *   1.5  → 150% scale → 144 DPI → adds class "dpi-144"
 *   1.75 → 175% scale → 168 DPI → adds class "dpi-168"
 *   2.0+ → 200% scale → 192 DPI → adds class "dpi-192"
 *
 * CSS variables defined in index.css change per class so all elements
 * using those variables automatically scale to fit the screen.
 */
export function useDpiClass() {
  useEffect(() => {
    const dpr = window.devicePixelRatio || 1;

    let cls;
    if      (dpr < 1.15) cls = "dpi-96";
    else if (dpr < 1.35) cls = "dpi-120";
    else if (dpr < 1.65) cls = "dpi-144";
    else if (dpr < 1.85) cls = "dpi-168";
    else                 cls = "dpi-192";

    document.documentElement.classList.add(cls);

    // Clean up on unmount (useful in dev hot-reload)
    return () => document.documentElement.classList.remove(cls);
  }, []);
}
