const { EventEmitter } = require("events");
const WebSocket = require("ws");
const logger = require("./logger");

class OpenAiRealtimeClient extends EventEmitter {
  constructor(config) {
    super();
    this.config = config;
    this.socket = null;
    this.sessionConfigured = false;
    this.sessionReady = false;
  }

  connect(instructions) {
    const url = `${this.config.url}?model=${encodeURIComponent(this.config.model)}`;
    this.socket = new WebSocket(url, {
      headers: {
        Authorization: `Bearer ${this.config.apiKey}`
      }
    });

    this.socket.on("open", () => {
      this.sessionConfigured = false;
      this.sessionReady = false;
      logger.info("Connected to OpenAI Realtime.");
    });

    this.socket.on("message", (raw) => {
      this.handleMessage(raw, instructions);
    });

    this.socket.on("error", (error) => {
      this.emit("error", error);
    });

    this.socket.on("close", (code) => {
      this.emit("close", code);
    });
  }

  close() {
    if (this.socket) {
      this.socket.close();
      this.socket = null;
    }

    this.sessionConfigured = false;
    this.sessionReady = false;
  }

  send(event) {
    if (!this.socket || this.socket.readyState !== WebSocket.OPEN) {
      throw new Error("Realtime socket is not open");
    }

    this.socket.send(JSON.stringify(event));
  }

  handleMessage(raw, instructions) {
    const event = JSON.parse(raw.toString("utf8"));
    this.emit("server_event", event);

    if (event.type === "session.created" && !this.sessionConfigured) {
      this.sessionConfigured = true;
      this.configureSession(instructions);
      this.sessionReady = true;
      this.emit("session_ready", event);
      return;
    }

    if (event.type === "session.updated") {
      this.emit("session_updated", event);
      return;
    }

    if (event.type === "response.created") {
      this.emit("assistant_response_started", event);
      return;
    }

    if (event.type === "response.output_audio.delta" || event.type === "response.audio.delta") {
      this.emit("assistant_audio_chunk", {
        raw: event,
        bytes: Buffer.from(event.delta, "base64")
      });
      return;
    }

    if (event.type === "response.done") {
      this.emit("assistant_response_finished", event);
      return;
    }

    if (typeof event.type === "string" && event.type.includes("transcript")) {
      this.emit("transcript_event", event);
    }
  }

  configureSession(instructions) {
    const session = {
      type: "realtime",
      model: this.config.model,
      output_modalities: ["audio"],
      audio: {
        input: {
          format: {
            type: "audio/pcm",
            rate: this.config.inputSampleRate
          },
          turn_detection: {
            type: this.config.turnDetection,
            interrupt_response: false
          }
        },
        output: {
          format: {
            type: "audio/pcm",
            rate: this.config.outputSampleRate
          },
          voice: this.config.voice
        }
      }
    };

    if (instructions && instructions.trim()) {
      session.instructions = instructions.trim();
    }

    this.send({
      type: "session.update",
      session
    });

    logger.info("Sent realtime session.update", {
      voice: this.config.voice,
      turnDetection: this.config.turnDetection
    });
  }

  buildAudioResponseConfig() {
    return {
      output_modalities: ["audio"],
      audio: {
        output: {
          format: {
            type: "audio/pcm",
            rate: this.config.outputSampleRate
          },
          voice: this.config.voice
        }
      }
    };
  }

  appendInputAudio(buffer) {
    this.send({
      type: "input_audio_buffer.append",
      audio: buffer.toString("base64")
    });
  }

  sendInputText(text) {
    this.send({
      type: "conversation.item.create",
      item: {
        type: "message",
        role: "user",
        content: [
          {
            type: "input_text",
            text
          }
        ]
      }
    });

    this.send({
      type: "response.create",
      response: this.buildAudioResponseConfig()
    });
  }

  createResponse(payload) {
    const response = payload && Object.keys(payload).length
      ? {
          ...this.buildAudioResponseConfig(),
          ...payload
        }
      : this.buildAudioResponseConfig();

    if (response.audio && response.audio.output) {
      response.audio = {
        ...this.buildAudioResponseConfig().audio,
        ...response.audio,
        output: {
          ...this.buildAudioResponseConfig().audio.output,
          ...response.audio.output
        }
      };
    }

    this.send({
      type: "response.create",
      response
    });
  }
}

module.exports = {
  OpenAiRealtimeClient
};
