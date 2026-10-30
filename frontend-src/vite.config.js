import {defineConfig} from 'vite';import react from '@vitejs/plugin-react';
// Builds straight into ../frontend, which the C++ server serves. `npm run dev` proxies /api to the C++ server.
export default defineConfig({plugins:[react()],build:{outDir:'../frontend',emptyOutDir:true},server:{proxy:{'/api':'http://localhost:8080'}}});
