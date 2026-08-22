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
  // Honour PORT when the environment sets one, so a preview harness can assign
  // a free port and still find the server. No hardcoded default, so several
  // sessions can preview the same project at once.
  server: {
    host: true,
    port: process.env.PORT ? Number(process.env.PORT) : undefined,
    strictPort: Boolean(process.env.PORT),
  },
  // The Emscripten glue is already an ES module; leave it alone.
  optimizeDeps: { exclude: ["/wasm/aerolab.js"] },
});
