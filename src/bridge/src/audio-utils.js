function pcm16ByteLength(sampleRate, durationMs) {
  return Math.floor((sampleRate * durationMs * 2) / 1000);
}

function writeWavFileHeader(buffer, dataSize, sampleRate, channels, bitsPerSample) {
  const byteRate = sampleRate * channels * (bitsPerSample / 8);
  const blockAlign = channels * (bitsPerSample / 8);

  buffer.write("RIFF", 0);
  buffer.writeUInt32LE(36 + dataSize, 4);
  buffer.write("WAVE", 8);
  buffer.write("fmt ", 12);
  buffer.writeUInt32LE(16, 16);
  buffer.writeUInt16LE(1, 20);
  buffer.writeUInt16LE(channels, 22);
  buffer.writeUInt32LE(sampleRate, 24);
  buffer.writeUInt32LE(byteRate, 28);
  buffer.writeUInt16LE(blockAlign, 32);
  buffer.writeUInt16LE(bitsPerSample, 34);
  buffer.write("data", 36);
  buffer.writeUInt32LE(dataSize, 40);
}

function wrapPcmAsWav(pcmBuffer, sampleRate) {
  const header = Buffer.alloc(44);
  writeWavFileHeader(header, pcmBuffer.length, sampleRate, 1, 16);
  return Buffer.concat([header, pcmBuffer]);
}

module.exports = {
  pcm16ByteLength,
  wrapPcmAsWav
};
