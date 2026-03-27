const fs = require("fs");
const path = require("path");
const { wrapPcmAsWav } = require("./audio-utils");

function ensureDir(dirPath) {
  fs.mkdirSync(dirPath, { recursive: true });
}

function timestampName(date = new Date()) {
  const pad = (value) => String(value).padStart(2, "0");
  return [
    date.getFullYear(),
    pad(date.getMonth() + 1),
    pad(date.getDate())
  ].join("-") + "_" + [pad(date.getHours()), pad(date.getMinutes()), pad(date.getSeconds())].join("-");
}

class FileSessionStore {
  constructor(runtimeDir, outputSampleRate) {
    this.runtimeDir = runtimeDir;
    this.outputSampleRate = outputSampleRate;
    this.sessionDir = null;
    this.eventsPath = null;
    this.transcriptPath = null;
  }

  initSession() {
    const sessionName = timestampName();
    const sessionsDir = path.join(this.runtimeDir, "sessions");
    const memoryDir = path.join(this.runtimeDir, "memory");

    ensureDir(sessionsDir);
    ensureDir(memoryDir);

    this.sessionDir = path.join(sessionsDir, sessionName);
    ensureDir(this.sessionDir);

    this.eventsPath = path.join(this.sessionDir, "events.jsonl");
    this.transcriptPath = path.join(this.sessionDir, "transcript.md");

    fs.writeFileSync(this.transcriptPath, "# Transcript\n\n", "utf8");
    return this.sessionDir;
  }

  getRollingSummary() {
    const summaryPath = path.join(this.runtimeDir, "memory", "rolling-summary.md");
    if (!fs.existsSync(summaryPath)) {
      return "";
    }

    return fs.readFileSync(summaryPath, "utf8").trim();
  }

  appendEvent(event) {
    if (!this.eventsPath) {
      return;
    }

    fs.appendFileSync(this.eventsPath, `${JSON.stringify(event)}\n`, "utf8");
  }

  appendTranscript(role, text) {
    if (!this.transcriptPath || !text) {
      return;
    }

    const header = role === "assistant" ? "Assistant" : "User";
    fs.appendFileSync(this.transcriptPath, `## ${header}\n\n${text.trim()}\n\n`, "utf8");
  }

  saveAssistantAudio(turnId, pcmChunks) {
    if (!this.sessionDir || !pcmChunks.length) {
      return null;
    }

    const pcm = Buffer.concat(pcmChunks);
    const wav = wrapPcmAsWav(pcm, this.outputSampleRate);
    const filePath = path.join(this.sessionDir, `assistant-turn-${String(turnId).padStart(4, "0")}.wav`);
    fs.writeFileSync(filePath, wav);
    return filePath;
  }
}

module.exports = {
  FileSessionStore
};
