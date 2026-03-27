const logger = require("./logger");
const { loadConfig } = require("./config");
const { BridgeApp } = require("./bridge-app");

async function main() {
  const config = loadConfig();
  const app = new BridgeApp(config);
  await app.start();

  const shutdown = async (signal) => {
    logger.info(`Received ${signal}, shutting down bridge.`);
    await app.stop();
    process.exit(0);
  };

  process.on("SIGINT", () => {
    shutdown("SIGINT");
  });

  process.on("SIGTERM", () => {
    shutdown("SIGTERM");
  });
}

main().catch((error) => {
  logger.error("Bridge failed to start", error);
  process.exit(1);
});
