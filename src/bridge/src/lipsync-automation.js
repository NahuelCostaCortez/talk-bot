const { spawn } = require("child_process");
const { EventEmitter } = require("events");

function summarizeOutput(chunks) {
  if (!chunks.length) {
    return "";
  }

  return Buffer.concat(chunks).toString("utf8").trim().slice(0, 4000);
}

class LipsyncAutomation extends EventEmitter {
  constructor(options) {
    super();
    this.command = options.command || null;
    this.timeoutMs = options.timeoutMs;
    this.queue = Promise.resolve();
  }

  isEnabled() {
    return Boolean(this.command);
  }

  enqueue(payload) {
    if (!this.isEnabled() || !payload || !payload.wavPath) {
      return Promise.resolve(null);
    }

    const taskPromise = this.queue.then(() => this.run(payload));
    this.queue = taskPromise.catch(() => null);
    return taskPromise;
  }

  run(payload) {
    return new Promise((resolve, reject) => {
      const stdoutChunks = [];
      const stderrChunks = [];
      const child = spawn(this.command, {
        cwd: process.cwd(),
        env: {
          ...process.env,
          TB_LIPSYNC_TURN_ID: String(payload.turnId || ""),
          TB_LIPSYNC_TRANSCRIPT: payload.transcript || "",
          TB_LIPSYNC_WAV_PATH: payload.wavPath,
          TB_LIPSYNC_SESSION_DIR: payload.sessionDir || "",
          TB_LIPSYNC_SAMPLE_RATE: String(payload.sampleRate || "")
        },
        shell: true,
        stdio: ["ignore", "pipe", "pipe"],
        windowsHide: true
      });

      this.emit("started", {
        turnId: payload.turnId,
        wavPath: payload.wavPath
      });

      let finished = false;
      let timeoutHandle = null;

      const settle = (callback, value) => {
        if (finished) {
          return;
        }

        finished = true;
        if (timeoutHandle) {
          clearTimeout(timeoutHandle);
        }
        callback(value);
      };

      child.stdout.on("data", (chunk) => {
        stdoutChunks.push(Buffer.from(chunk));
      });

      child.stderr.on("data", (chunk) => {
        stderrChunks.push(Buffer.from(chunk));
      });

      child.on("error", (error) => {
        settle(reject, error);
      });

      child.on("close", (code, signal) => {
        const result = {
          code,
          signal,
          stdout: summarizeOutput(stdoutChunks),
          stderr: summarizeOutput(stderrChunks)
        };

        if (code === 0) {
          settle(resolve, result);
          return;
        }

        const error = new Error(
          `Lipsync command exited with code ${code}${signal ? ` (signal: ${signal})` : ""}`
        );
        error.result = result;
        settle(reject, error);
      });

      if (this.timeoutMs > 0) {
        timeoutHandle = setTimeout(() => {
          child.kill();
          const error = new Error(`Lipsync command timed out after ${this.timeoutMs} ms`);
          error.result = {
            code: null,
            signal: "SIGTERM",
            stdout: summarizeOutput(stdoutChunks),
            stderr: summarizeOutput(stderrChunks)
          };
          settle(reject, error);
        }, this.timeoutMs);
      }
    });
  }
}

module.exports = {
  LipsyncAutomation
};
