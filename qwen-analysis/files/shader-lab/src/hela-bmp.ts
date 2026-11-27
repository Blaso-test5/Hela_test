import * as fs from "node:fs";

export interface BmpPixelAnalysis {
  path: string;
  valid: boolean;
  error?: string;
  width: number;
  height: number;
  orientation?: "top-down" | "bottom-up";
  bitsPerPixel: number;
  totalPixels: number;
  nonBlackPixels: number;
  nonBlackRatio: number;
  maxChannelValue: number;
}

/**
 * Analizza un BMP non compresso BI_RGB (24-bit o 32-bit, sia bottom-up
 * che top-down con biHeight negativo come emesso da `SaveMappedTextureAsBmp` in `dxgi_proxy.cpp`)
 * senza dipendenze esterne.
 *
 * Conta i pixel con (R | G | B) != 0. L'osservazione di pixel non neri NON equivale da sola
 * a validazione visiva del menu di gioco.
 */
export function analyzeBmpBuffer(buf: Buffer, filePath = "<buffer>"): BmpPixelAnalysis {
  const invalid = (error: string): BmpPixelAnalysis => ({
    path: filePath,
    valid: false,
    error,
    width: 0,
    height: 0,
    bitsPerPixel: 0,
    totalPixels: 0,
    nonBlackPixels: 0,
    nonBlackRatio: 0,
    maxChannelValue: 0,
  });

  if (buf.length < 54 || buf.toString("ascii", 0, 2) !== "BM") {
    return invalid("Header BMP non valido (manca firma 'BM' o file troncato).");
  }

  const pixelOffset = buf.readUInt32LE(10);
  const dibSize = buf.readUInt32LE(14);
  if (dibSize < 40 || pixelOffset >= buf.length) {
    return invalid(`Header DIB non supportato (dibSize=${dibSize}, pixelOffset=${pixelOffset}).`);
  }

  const width = buf.readInt32LE(18);
  const rawHeight = buf.readInt32LE(22);
  const height = Math.abs(rawHeight);
  const orientation: "top-down" | "bottom-up" = rawHeight < 0 ? "top-down" : "bottom-up";
  const bpp = buf.readUInt16LE(28);
  const compression = buf.readUInt32LE(30);

  if (width <= 0 || height <= 0) {
    return invalid(`Dimensioni BMP non valide (${width}x${rawHeight}).`);
  }
  if ((bpp !== 24 && bpp !== 32) || compression !== 0) {
    return invalid(`Formato BMP non supportato: ${bpp}bpp compression=${compression} (atteso 24/32bpp non compresso).`);
  }

  const bytesPerPixel = bpp / 8;
  const rowStride = Math.ceil((width * bytesPerPixel) / 4) * 4;
  const requiredBytes = pixelOffset + rowStride * height;
  if (buf.length < requiredBytes) {
    return invalid(`BMP troncato: attesi almeno ${requiredBytes} byte, trovati ${buf.length}.`);
  }

  let nonBlackPixels = 0;
  let maxChannelValue = 0;
  for (let y = 0; y < height; y++) {
    const rowStart = pixelOffset + y * rowStride;
    for (let x = 0; x < width; x++) {
      const p = rowStart + x * bytesPerPixel;
      const b = buf[p];
      const g = buf[p + 1];
      const r = buf[p + 2];
      if ((b | g | r) !== 0) {
        nonBlackPixels++;
        if (r > maxChannelValue) maxChannelValue = r;
        if (g > maxChannelValue) maxChannelValue = g;
        if (b > maxChannelValue) maxChannelValue = b;
      }
    }
  }

  const totalPixels = width * height;
  return {
    path: filePath,
    valid: true,
    width,
    height,
    orientation,
    bitsPerPixel: bpp,
    totalPixels,
    nonBlackPixels,
    nonBlackRatio: totalPixels > 0 ? nonBlackPixels / totalPixels : 0,
    maxChannelValue,
  };
}

export function analyzeBmpFile(filePath: string): BmpPixelAnalysis {
  if (!fs.existsSync(filePath)) {
    return {
      path: filePath,
      valid: false,
      error: `File BMP non trovato: ${filePath}`,
      width: 0,
      height: 0,
      bitsPerPixel: 0,
      totalPixels: 0,
      nonBlackPixels: 0,
      nonBlackRatio: 0,
      maxChannelValue: 0,
    };
  }
  return analyzeBmpBuffer(fs.readFileSync(filePath), filePath);
}
