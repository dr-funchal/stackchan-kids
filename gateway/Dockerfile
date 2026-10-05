FROM node:24-alpine

ENV NODE_ENV=production
WORKDIR /app

COPY package.json package-lock.json ./
RUN npm ci --omit=dev && npm cache clean --force

COPY src ./src

# Story library lives in a bind-mounted volume (see docker-compose.yml); this is only the fallback.
COPY data ./data

USER node
HEALTHCHECK --interval=30s --timeout=5s --start-period=20s --retries=3 \
  CMD ["node", "--disable-warning=ExperimentalWarning", "src/healthcheck.ts"]
CMD ["node", "--disable-warning=ExperimentalWarning", "src/index.ts"]
