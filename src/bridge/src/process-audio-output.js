const { spawn } = require("child_process");
const logger = require("./logger");

function spawnShell(command) {
  return spawn("cmd.exe", ["/d", "/s", "/c", command], {
    stdio: ["pipe", "ignore", "pipe"],
    windowsHide: true
  });
}

class ProcessAudioOutput {
  constructor(command) {
    this.command = command;
    this.child = null;
  }

  ensureStarted() {
    if (!this.command || this.child) {
      return;
    }

    logger.info("Starting speaker output process.");
    this.child = spawnShell(this.command);
    this.child.stderr.on("data", (chunk) => {
      logger.debug("Speaker stderr", chunk.toString("utf8"));
    });

    this.child.on("exit", (code) => {
      logger.warn(`Speaker process exited with code ${code}.`);
      this.child = null;
    });
  }

  write(chunk) {
    if (!this.command) {
      return;
    }

    this.ensureStarted();
    if (!this.child || !this.child.stdin.writable) {
      return;
    }

    this.child.stdin.write(chunk);
  }

  stop() {
    if (!this.child) {
      return;
    }

    if (this.child.stdin.writable) {
      this.child.stdin.end();
    }

    this.child.kill();
    this.child = null;
  }
}

module.exports = {
  ProcessAudioOutput
};
