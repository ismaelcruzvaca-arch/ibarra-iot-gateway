# Multi-stage lightweight system monitor container build
FROM node:20-alpine AS builder

WORKDIR /app

COPY package*.json ./
RUN npm ci --only=production

FROM node:20-alpine AS runner

WORKDIR /app

COPY --from=builder /app/node_modules ./node_modules
COPY package*.json ./
COPY system_monitor.js ./
COPY system_monitor_logic.js ./

# Run under secure non-privileged container account
USER node

ENV NODE_ENV=production
ENV HOST_FS_PATH=/hostfs

CMD ["node", "system_monitor.js"]
