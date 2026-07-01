FROM python:3.12-slim

WORKDIR /app

# 这些文件由 scripts/prepare-deploy.* 拷贝到 build context（与 Dockerfile 同级）
COPY server.py /app/server.py
COPY krkrsdl2.js /app/krkrsdl2.js
COPY krkrsdl2.wasm /app/krkrsdl2.wasm
COPY index.html /app/index.html
COPY play.html /app/play.html

# games / covers / manifest.json 通过卷挂载提供，这里只建空目录占位
RUN mkdir -p /app/games /app/covers

ENV PORT=8080
EXPOSE 8080

CMD ["sh", "-c", "exec python /app/server.py /app \"$PORT\""]
