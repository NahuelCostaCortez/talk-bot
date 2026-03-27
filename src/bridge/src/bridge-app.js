const path = require("path");
const logger = require("./logger");
const { AssistantAudioBuffer } = require("./assistant-audio-buffer");
const { ProcessAudioInput } = require("./process-audio-input");
const { ProcessAudioOutput } = require("./process-audio-output");
const { FileSessionStore } = require("./file-session-store");
const { LipsyncAutomation } = require("./lipsync-automation");
const { LocalControlServer } = require("./local-control-server");
const { OpenAiRealtimeClient } = require("./openai-realtime-client");

function buildInstructions(config, rollingSummary) {
  const parts = [];

  if (config.prompt.instructions) {
    parts.push(config.prompt.instructions);
  }

  if (rollingSummary) {
    parts.push("Conversation memory from previous sessions:");
    parts.push(rollingSummary);
  }

  return parts.join("\n\n").trim();
}

function extractTranscriptText(event) {
  if (!event) {
    return "";
  }

  if (typeof event.transcript === "string") {
    return event.transcript;
  }

  if (event.response && Array.isArray(event.response.output)) {
    const pieces = [];

    for (const outputItem of event.response.output) {
      if (!Array.isArray(outputItem.content)) {
        continue;
      }

      for (const contentPart of outputItem.content) {
        if (typeof contentPart.transcript === "string") {
          pieces.push(contentPart.transcript);
        } else if (typeof contentPart.text === "string") {
          pieces.push(contentPart.text);
        }
      }
    }

    return pieces.join("\n").trim();
  }

  return "";
}

class BridgeApp {
  constructor(config) {
    this.config = config;
    this.store = new FileSessionStore(
      path.resolve(config.bridge.runtimeDir),
      config.openAi.outputSampleRate
    );
    this.localServer = new LocalControlServer(
      config.bridge.controlHost,
      config.bridge.controlPort
    );
    this.realtimeClient = new OpenAiRealtimeClient(config.openAi);
    this.micInput = new ProcessAudioInput(config.audio.micCommand);
    this.speakerOutput = new ProcessAudioOutput(config.audio.speakerCommand);
    this.audioBuffer = new AssistantAudioBuffer(
      config.openAi.outputSampleRate,
      config.bridge.responseBufferMs
    );
    this.lipsyncAutomation = new LipsyncAutomation(config.lipsync);
    this.assistantTurnIndex = 0;
    this.currentAssistantChunks = [];
  }

  async start() {
    const sessionDir = this.store.initSession();
    const rollingSummary = this.store.getRollingSummary();
    const instructions = buildInstructions(this.config, rollingSummary);

    logger.info(`Session artifacts will be written to ${sessionDir}`);
    logger.info(`OpenAI Realtime voice configured as ${this.config.openAi.voice}`);

    this.wireEvents();
    this.localServer.start();
    this.realtimeClient.connect(instructions);
  }

  wireEvents() {
    this.lipsyncAutomation.on("started", ({ turnId, wavPath }) => {
      this.store.appendEvent({
        type: "assistant.lipsync_started",
        at: new Date().toISOString(),
        turnId,
        wavPath
      });
      this.localServer.broadcast({
        type: "assistant.lipsync_started",
        payload: {
          turnId,
          wavPath
        }
      });
    });

    this.localServer.on("input_text", (payload) => {
      const text = typeof payload === "string" ? payload : payload && payload.text;
      if (!text) {
        return;
      }

      this.store.appendTranscript("user", text);
      this.store.appendEvent({ type: "local.input_text", text, at: new Date().toISOString() });
      this.realtimeClient.sendInputText(text);
    });

    this.localServer.on("response.create", (payload) => {
      this.realtimeClient.createResponse(payload);
    });

    this.realtimeClient.on("session_ready", (event) => {
      const createdVoice =
        event &&
        event.session &&
        event.session.audio &&
        event.session.audio.output &&
        event.session.audio.output.voice;

      this.store.appendEvent(event);
      this.localServer.broadcast({ type: "session.created", payload: event });
      if (createdVoice) {
        logger.info(`OpenAI session.created default voice ${createdVoice}`);
      }
      this.micInput.start();
    });

    this.realtimeClient.on("session_updated", (event) => {
      const effectiveVoice =
        event &&
        event.session &&
        event.session.audio &&
        event.session.audio.output &&
        event.session.audio.output.voice;

      this.store.appendEvent(event);
      if (effectiveVoice) {
        logger.info(`OpenAI confirmed realtime voice ${effectiveVoice}`);
      }
    });

    this.realtimeClient.on("assistant_response_started", (event) => {
      const responseVoice =
        event &&
        event.response &&
        event.response.audio &&
        event.response.audio.output &&
        event.response.audio.output.voice;

      this.assistantTurnIndex += 1;
      this.currentAssistantChunks = [];
      this.audioBuffer.reset();
      if (responseVoice) {
        logger.info(`OpenAI created response with voice ${responseVoice}`);
      }
      this.localServer.broadcast({
        type: "assistant.response_started",
        payload: { turnId: this.assistantTurnIndex }
      });
      this.store.appendEvent(event);
    });

    this.realtimeClient.on("assistant_audio_chunk", ({ raw, bytes }) => {
      this.currentAssistantChunks.push(bytes);
      this.store.appendEvent({
        type: raw.type,
        event_id: raw.event_id || null,
        length: bytes.length,
        at: new Date().toISOString()
      });
      this.audioBuffer.push(bytes);
    });

    this.realtimeClient.on("assistant_response_finished", (event) => {
      const turnId = this.assistantTurnIndex;
      this.audioBuffer.flush();
      const wavPath = this.store.saveAssistantAudio(
        turnId,
        this.currentAssistantChunks
      );
      const transcript = extractTranscriptText(event);

      if (transcript) {
        this.store.appendTranscript("assistant", transcript);
        this.localServer.broadcast({
          type: "assistant.transcript_final",
          payload: {
            turnId,
            transcript
          }
        });
      }

      this.store.appendEvent(event);
      this.localServer.broadcast({
        type: "assistant.response_finished",
        payload: {
          turnId,
          transcript,
          wavPath
        }
      });

      if (wavPath && this.lipsyncAutomation.isEnabled()) {
        const lipsyncPayload = {
          turnId,
          transcript,
          wavPath,
          sessionDir: this.store.sessionDir,
          sampleRate: this.config.openAi.outputSampleRate
        };

        this.store.appendEvent({
          type: "assistant.lipsync_queued",
          at: new Date().toISOString(),
          ...lipsyncPayload
        });

        this.lipsyncAutomation.enqueue(lipsyncPayload)
          .then((result) => {
            if (!result) {
              return;
            }

            this.store.appendEvent({
              type: "assistant.lipsync_completed",
              at: new Date().toISOString(),
              turnId,
              wavPath,
              result
            });
            this.localServer.broadcast({
              type: "assistant.lipsync_completed",
              payload: {
                turnId,
                wavPath,
                result
              }
            });
          })
          .catch((error) => {
            const result = error.result || null;
            this.store.appendEvent({
              type: "assistant.lipsync_failed",
              at: new Date().toISOString(),
              turnId,
              wavPath,
              message: error.message,
              result
            });
            this.localServer.broadcast({
              type: "assistant.lipsync_failed",
              payload: {
                turnId,
                wavPath,
                message: error.message,
                result
              }
            });
            logger.error("Lipsync automation failed", error.message);
          });
      }
    });

    this.realtimeClient.on("transcript_event", (event) => {
      this.store.appendEvent(event);
      this.localServer.broadcast({
        type: "assistant.transcript_event",
        payload: event
      });
    });

    this.realtimeClient.on("server_event", (event) => {
      if (event.type === "input_audio_buffer.speech_started") {
        this.localServer.broadcast({ type: "user.speech_started", payload: event });
      } else if (event.type === "input_audio_buffer.speech_stopped") {
        this.localServer.broadcast({ type: "user.speech_stopped", payload: event });
      }
    });

    this.realtimeClient.on("error", (error) => {
      logger.error("OpenAI realtime error", error.message);
      this.localServer.broadcast({
        type: "bridge.error",
        payload: { source: "openai", message: error.message }
      });
    });

    this.realtimeClient.on("close", (code) => {
      logger.warn(`OpenAI realtime socket closed with code ${code}.`);
      this.localServer.broadcast({
        type: "bridge.warning",
        payload: { source: "openai", code }
      });
    });

    this.micInput.on("chunk", (chunk) => {
      try {
        this.realtimeClient.appendInputAudio(chunk);
      } catch (error) {
        logger.warn("Dropping microphone chunk because socket is not ready.", error.message);
      }
    });

    this.micInput.on("exit", (code) => {
      this.localServer.broadcast({
        type: "bridge.warning",
        payload: { source: "mic", code }
      });
    });

    this.audioBuffer.on("chunk", (chunk) => {
      this.speakerOutput.write(chunk);
      this.localServer.broadcast({
        type: "assistant.audio_chunk",
        payload: {
          turnId: this.assistantTurnIndex,
          sampleRate: this.config.openAi.outputSampleRate,
          encoding: "pcm16",
          audioBase64: chunk.toString("base64")
        }
      });
    });

    this.realtimeClient.on("server_event", (event) => {
      if (event.type === "error") {
        this.store.appendEvent(event);
        logger.error("OpenAI server rejected a realtime event", event);
      }
    });
  }

  async stop() {
    this.micInput.stop();
    this.speakerOutput.stop();
    this.realtimeClient.close();
    this.localServer.stop();
  }
}

module.exports = {
  BridgeApp
};
