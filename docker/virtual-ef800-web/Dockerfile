# syntax=docker/dockerfile:1.7
FROM python:3.15-slim-bookworm@sha256:96599df822d0e1c6bb700014a05c7f3c8cabc6e0f7ff3c4f9f69cee033b4b5d9 AS runtime
RUN useradd --system --home-dir /app --shell /usr/sbin/nologin virtualef800
WORKDIR /app
COPY GSE/Virtual-EF800/requirements.txt ./requirements.txt
RUN python -m pip install --no-cache-dir -r requirements.txt
COPY GSE/Virtual-EF800 ./virtual-ef800
USER virtualef800
ENV PYTHONPATH=/app/virtual-ef800 WEB_HOST=0.0.0.0 WEB_PORT=8080 SIMULATOR_HOST=virtual-ef800 SIMULATOR_PORT=8800
EXPOSE 8080
WORKDIR /app/virtual-ef800
ENTRYPOINT ["python", "gateway.py"]
