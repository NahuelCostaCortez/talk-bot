const { EventEmitter } = require("events");
const WebSocket = require("ws");
const logger = require("./logger");

class LocalControlServer extends EventEmitter {
  constructor(host, port) {
    super();
    this.host = host;
    this.port = port;
    this.server = null;
    this.clients = new Set();
  }

  start() {
    this.server = new WebSocket.Server({
      host: this.host,
      port: this.port
    });

    this.server.on("connection", (socket) => {
      this.clients.add(socket);
      logger.info("Local client connected.");
      this.sendTo(socket, {
        type: "bridge.ready",
        payload: {
          host: this.host,
          port: this.port
        }
      });

      socket.on("message", (raw) => {
        this.handleClientMessage(socket, raw);
      });

      socket.on("close", () => {
        this.clients.delete(socket);
      });
    });
  }

  stop() {
    for (const client of this.clients) {
      client.close();
    }

    if (this.server) {
      this.server.close();
      this.server = null;
    }
  }

  handleClientMessage(socket, raw) {
    try {
      const message = JSON.parse(raw.toString("utf8"));

      if (message.type === "ping") {
        this.sendTo(socket, { type: "pong", payload: { now: new Date().toISOString() } });
        return;
      }

      if (message.type === "input_text") {
        this.emit("input_text", message.payload);
        return;
      }

      if (message.type === "response.create") {
        this.emit("response.create", message.payload || {});
      }
    } catch (error) {
      logger.warn("Invalid JSON from local client.", error.message);
    }
  }

  broadcast(message) {
    const serialized = JSON.stringify(message);
    for (const client of this.clients) {
      if (client.readyState === WebSocket.OPEN) {
        client.send(serialized);
      }
    }
  }

  sendTo(socket, message) {
    if (socket.readyState === WebSocket.OPEN) {
      socket.send(JSON.stringify(message));
    }
  }
}

module.exports = {
  LocalControlServer
};
