import { useEffect } from "react";

/**
 * useViewportScale
 * ----------------
 * Computes a single --scale CSS variable based on the actual
 * window (viewport) size vs. the design reference size.
 *
 * Design reference: 1366 × 768  (typical 100%-scale laptop)
 *
 * How it works:
 *   scale = min(innerWidth / 1366,  innerHeight / 768)
 *
 * Examples:
 *   1366×768  (100% DPI)  → scale = 1.000  (no change)
 *   1024×576  (75%)       → scale = 0.750  (proportionally smaller)
 *   1920×1080 (141%)      → scale = 1.406  (proportionally larger)
 *   853×480   (150% DPI)  → scale = 0.625  (window is smaller in CSS px)
 *
 * All CSS layout tokens in index.css use calc(Xpx * var(--scale))
 * so EVERY element scales identically → same visual appearance at any DPI.
 *
 * Also listens to window resize so the scale stays correct if the
 * user resizes the Tauri window.
 */

const DESIGN_W = 1366;
const DESIGN_H = 768;

export function useViewportScale() {
  useEffect(() => {
    const apply = () => {
      const scaleX = window.innerWidth  / DESIGN_W;
      const scaleY = window.innerHeight / DESIGN_H;
      const scale  = Math.min(scaleX, scaleY);

      document.documentElement.style.setProperty("--scale", scale.toFixed(4));
    };

    apply();
    window.addEventListener("resize", apply);
    return () => window.removeEventListener("resize", apply);
  }, []);
}
