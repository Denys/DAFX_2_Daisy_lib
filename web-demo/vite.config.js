import { defineConfig } from 'vite';

export default defineConfig({
    base: './',
    build: {
        outDir: 'dist',
        assetsDir: 'assets',
        target: 'esnext'
    },
    server: {
        port: 5173,
        headers: {
            'Cross-Origin-Opener-Policy': 'same-origin',
            'Cross-Origin-Embedder-Policy': 'require-corp'
        }
    },
    optimizeDeps: {
        exclude: ['dafx-effects']
    }
});
