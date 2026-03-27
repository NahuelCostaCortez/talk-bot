const { EventEmitter } = require("events");
const { pcm16ByteLength } = require("./audio-utils");

class AssistantAudioBuffer extends EventEmitter {
  constructor(sampleRate, thresholdMs) {
    super();
    this.sampleRate = sampleRate;
    this.thresholdBytes = pcm16ByteLength(sampleRate, thresholdMs);
    this.pending = [];
    this.pendingBytes = 0;
    this.primed = false;
  }

  reset() {
    this.pending = [];
    this.pendingBytes = 0;
    this.primed = false;
  }

  push(chunk) {
    if (!chunk || !chunk.length) {
      return;
    }

    if (this.primed) {
      this.emit("chunk", chunk);
      return;
    }

    this.pending.push(chunk);
    this.pendingBytes += chunk.length;

    if (this.pendingBytes < this.thresholdBytes) {
      return;
    }

    this.primed = true;
    for (const pendingChunk of this.pending) {
      this.emit("chunk", pendingChunk);
    }

    this.pending = [];
    this.pendingBytes = 0;
  }

  flush() {
    if (!this.pending.length) {
      return;
    }

    for (const pendingChunk of this.pending) {
      this.emit("chunk", pendingChunk);
    }

    this.pending = [];
    this.pendingBytes = 0;
    this.primed = true;
  }
}

module.exports = {
  AssistantAudioBuffer
};
