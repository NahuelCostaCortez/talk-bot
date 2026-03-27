const fs = require("fs");
const path = require("path");

function parseEnvFile(envPath) {
  if (!fs.existsSync(envPath)) {
    return;
  }

  const raw = fs.readFileSync(envPath, "utf8");
  for (const line of raw.split(/\r?\n/)) {
    const trimmed = line.trim();
    if (!trimmed || trimmed.startsWith("#")) {
      continue;
    }

    const separator = trimmed.indexOf("=");
    if (separator < 0) {
      continue;
    }

    const key = trimmed.slice(0, separator).trim();
    const value = trimmed.slice(separator + 1).trim();
    if (!(key in process.env)) {
      process.env[key] = value;
    }
  }
}

function parseIntEnv(name, fallback) {
  const raw = process.env[name];
  if (!raw) {
    return fallback;
  }

  const parsed = Number.parseInt(raw, 10);
  if (Number.isNaN(parsed)) {
    throw new Error(`Invalid integer for ${name}: ${raw}`);
  }

  return parsed;
}

function parseOptionalCommand(name) {
  const value = process.env[name];
  return value && value.trim() ? value.trim() : null;
}

function parseOptionalPositiveInt(name) {
  const raw = process.env[name];
  if (!raw) {
    return null;
  }

  const parsed = Number.parseInt(raw, 10);
  if (Number.isNaN(parsed) || parsed <= 0) {
    throw new Error(`Invalid positive integer for ${name}: ${raw}`);
  }

  return parsed;
}

function resolveRuntimePath(baseDir, inputPath) {
  if (path.isAbsolute(inputPath)) {
    return inputPath;
  }

  return path.resolve(baseDir, inputPath);
}

function readPromptFile(promptPath) {
  if (!promptPath || !fs.existsSync(promptPath)) {
    return "";
  }

  return fs.readFileSync(promptPath, "utf8").trim();
}

function loadConfig() {
  const baseDir = __dirname;
  parseEnvFile(path.resolve(baseDir, "..", ".env"));

  const apiKey = process.env.OPENAI_API_KEY;
  if (!apiKey) {
    throw new Error("OPENAI_API_KEY is required");
  }

  const runtimeDir = resolveRuntimePath(
    baseDir,
    process.env.TB_RUNTIME_DIR || "../runtime"
  );
  const promptFile = resolveRuntimePath(
    baseDir,
    process.env.TB_SYSTEM_PROMPT_FILE || "../../docs/guest-system-prompt.txt"
  );

  return {
    openAi: {
      apiKey,
      model: process.env.OPENAI_MODEL || "gpt-realtime",
      voice: process.env.OPENAI_VOICE || "coral",
      url: process.env.OPENAI_URL || "wss://api.openai.com/v1/realtime",
      inputSampleRate: parseIntEnv("TB_INPUT_SAMPLE_RATE", 24000),
      outputSampleRate: parseIntEnv("TB_OUTPUT_SAMPLE_RATE", 24000),
      turnDetection: process.env.TB_TURN_DETECTION || "semantic_vad"
    },
    bridge: {
      controlHost: process.env.TB_CONTROL_HOST || "127.0.0.1",
      controlPort: parseIntEnv("TB_CONTROL_PORT", 8765),
      responseBufferMs: parseIntEnv("TB_RESPONSE_BUFFER_MS", 250),
      runtimeDir
    },
    prompt: {
      filePath: promptFile,
      instructions: readPromptFile(promptFile)
    },
    audio: {
      micCommand: parseOptionalCommand("TB_MIC_COMMAND"),
      speakerCommand: parseOptionalCommand("TB_SPEAKER_COMMAND")
    },
    lipsync: {
      command: parseOptionalCommand("TB_LIPSYNC_COMMAND"),
      timeoutMs: parseOptionalPositiveInt("TB_LIPSYNC_TIMEOUT_MS") || 300000
    }
  };
}

module.exports = {
  loadConfig
};
