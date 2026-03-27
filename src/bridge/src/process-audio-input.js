const { EventEmitter } = require("events");
const { spawn } = require("child_process");
const logger = require("./logger");

function spawnShell(command) {
  return spawn("cmd.exe", ["/d", "/s", "/c", command], {
    stdio: ["ignore", "pipe", "pipe"],
    windowsHide: true
  });
}

class ProcessAudioInput extends EventEmitter {
  constructor(command) {
    super();
    this.command = command;
    this.child = null;
  }

  start() {
    if (!this.command) {
      logger.warn("No microphone command configured; mic capture is disabled.");
      return;
    }

    logger.info("Starting microphone capture process.");
    this.child = spawnShell(this.command);

    this.child.stdout.on("data", (chunk) => {
      this.emit("chunk", chunk);
    });

    this.child.stderr.on("data", (chunk) => {
      logger.debug("Microphone stderr", chunk.toString("utf8"));
    });

    this.child.on("exit", (code) => {
      logger.warn(`Microphone process exited with code ${code}.`);
      this.emit("exit", code);
    });
  }

  stop() {
    if (this.child) {
      this.child.kill();
      this.child = null;
    }
  }
}

module.exports = {
  ProcessAudioInput
};
