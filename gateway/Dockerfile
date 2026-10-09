FROM node:24-alpine

# ffmpeg decodes uploads; opusenc writes the exact Opus shape the robot plays
RUN apk add --no-cache ffmpeg opus-tools

ENV NODE_ENV=production
WORKDIR /app

COPY package.json package-lock.json ./
RUN npm ci --omit=dev && npm cache clean --force

COPY src ./src

# Seed stories; the real library lives in the bind-mounted data volume (see docker-compose.yml)
COPY data ./data

USER node
HEALTHCHECK --interval=30s --timeout=5s --start-period=20s --retries=3 \
  CMD ["node", "--disable-warning=ExperimentalWarning", "src/healthcheck.ts"]
CMD ["node", "--disable-warning=ExperimentalWarning", "src/index.ts"]
