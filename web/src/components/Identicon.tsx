import React, { useMemo } from 'react';

interface Props {
  agentId: number;
  size?: number;
  className?: string;
}

// Deterministic hash from agent_id
function hash(n: number): number[] {
  // Simple LCG-based expand: produce 8 pseudo-random bytes from agent_id
  const state: number[] = [];
  let s = (n * 2654435761) >>> 0; // Knuth multiplicative hash
  for (let i = 0; i < 8; i++) {
    s = Math.imul(s ^ (s >>> 16), 0x45d9f3b) >>> 0;
    s = Math.imul(s ^ (s >>> 16), 0x45d9f3b) >>> 0;
    s = (s ^ (s >>> 16)) >>> 0;
    state.push(s & 0xff);
  }
  return state;
}

// Convert hue/sat/light to css hsl
function hsl(h: number, s: number, l: number) {
  return `hsl(${h},${s}%,${l}%)`;
}

export function Identicon({ agentId, size = 36, className }: Props) {
  const svg = useMemo(() => {
    const h = hash(agentId);

    // Derive palette: 2 complementary colors
    const hue1 = (h[0] / 255) * 360;
    const hue2 = (hue1 + 150 + (h[1] / 255) * 60) % 360;
    const bg = hsl(hue1, 60, 30);
    const fg = hsl(hue2, 80, 70);

    // 5x5 symmetric grid (left half mirrored): bits from h[2..5]
    // cols 0-1 mirrored to cols 4-3, col 2 center
    const cells: boolean[] = [];
    const bits = (h[2] << 24) | (h[3] << 16) | (h[4] << 8) | h[5];
    for (let row = 0; row < 5; row++) {
      for (let col = 0; col < 3; col++) {
        cells[row * 5 + col] = Boolean((bits >> (row * 3 + col)) & 1);
      }
      // Mirror
      cells[row * 5 + 3] = cells[row * 5 + 1];
      cells[row * 5 + 4] = cells[row * 5 + 0];
    }

    const cell = size / 5;
    const rects = cells
      .map((on, i) => {
        if (!on) return null;
        const col = i % 5;
        const row = Math.floor(i / 5);
        return `<rect x="${col * cell}" y="${row * cell}" width="${cell}" height="${cell}" fill="${fg}" />`;
      })
      .filter(Boolean)
      .join('');

    return `<svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 ${size} ${size}">
  <rect width="${size}" height="${size}" fill="${bg}" rx="${size * 0.15}" />
  ${rects}
</svg>`;
  }, [agentId, size]);

  return (
    <div
      className={className}
      style={{ width: size, height: size, flexShrink: 0 }}
      dangerouslySetInnerHTML={{ __html: svg }}
    />
  );
}
