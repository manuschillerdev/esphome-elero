import { defineConfig } from 'vite'
import preact from '@preact/preset-vite'
import tailwindcss from '@tailwindcss/vite'
import { resolve } from 'path'

// Use DEVICE_IP env var to connect to real device, otherwise use mock server
// Example: DEVICE_IP=192.168.1.100 pnpm dev
const deviceTarget = process.env.DEVICE_IP
  ? `ws://${process.env.DEVICE_IP}`
  : 'ws://localhost:8080'

export default defineConfig({
  plugins: [preact(), tailwindcss()],
  resolve: {
    alias: {
      '@': resolve(__dirname, './src'),
      'react': 'preact/compat',
      'react-dom': 'preact/compat',
      'react/jsx-runtime': 'preact/jsx-runtime',
    },
  },
  server: {
    port: 3000,
    proxy: {
      '/elero/ws': {
        target: deviceTarget,
        ws: true,
        rewriteWsOrigin: true,
      },
    },
  },
  build: {
    outDir: 'dist',
    assetsDir: 'assets',
    minify: 'esbuild',
    target: 'es2020',
  },
})
