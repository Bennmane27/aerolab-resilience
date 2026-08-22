import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// base "./" so the built site works from any path, which is what GitHub Pages
// serves from (D-011, SYS-009: no backend, no absolute origin).
export default defineConfig({
  base: "./",
  plugins: [react()],
  build: {
    outDir: "dist",
    target: "es2022",
    chunkSizeWarningLimit: 1500,
  },
  server: { port: 5173, strictPort: false },
  // The Emscripten glue is already an ES module; leave it alone.
  optimizeDeps: { exclude: ["/wasm/aerolab.js"] },
});
