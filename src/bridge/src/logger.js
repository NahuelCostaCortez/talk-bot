function log(level, message, extra) {
  const timestamp = new Date().toISOString();
  if (extra === undefined) {
    console.log(`[${timestamp}] ${level}: ${message}`);
    return;
  }

  console.log(`[${timestamp}] ${level}: ${message}`, extra);
}

module.exports = {
  info(message, extra) {
    log("INFO", message, extra);
  },
  warn(message, extra) {
    log("WARN", message, extra);
  },
  error(message, extra) {
    log("ERROR", message, extra);
  },
  debug(message, extra) {
    if (process.env.TB_DEBUG === "1") {
      log("DEBUG", message, extra);
    }
  }
};
