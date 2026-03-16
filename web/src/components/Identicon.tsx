/**
 * Identicon — deterministic geometric avatar from agent_id.
 * Uses a simple hash to pick colors + a 5×5 symmetric grid pattern.
 */
import React from 'react';

interface IdenticonProps {
  agentId: number;
  size?: number;
  className?: string;
}

/** Fast integer hash (32-bit FNV-1a variant over the decimal string) */
function hash32(n: number): number {
  let h = 0x811c9dc5;
  const s = String(n);
  for (let i = 0; i < s.length; i++) {
    h ^= s.charCodeAt(i);
    h = Math.imul(h, 0x01000193);
  }
  return h >>> 0;
}

function hslToHex(h: number, s: number, l: number): string {
  s /= 100;
  l /= 100;
  const a = s * Math.min(l, 1 - l);
  const f = (n: number) => {
    const k = (n + h / 30) % 12;
    const c = l - a * Math.max(Math.min(k - 3, 9 - k, 1), -1);
    return Math.round(255 * c).toString(16).padStart(2, '0');
  };
  return `#${f(0)}${f(8)}${f(4)}`;
}

export function Identicon({ agentId, size = 36, className }: IdenticonProps) {
  const h0 = hash32(agentId);
  const h1 = hash32(agentId ^ 0xdeadbeef);

  const hue = h0 % 360;
  const fg = hslToHex(hue, 65, 50);
  const bg = hslToHex(hue, 20, 92);

  const cells: boolean[] = [];
  for (let row = 0; row < 5; row++) {
    for (let col = 0; col < 3; col++) {
      const bit = (h1 >> (row * 3 + col)) & 1;
      cells.push(bit === 1);
    }
  }

  const cellSize = size / 5;
  const rects: React.ReactElement[] = [];

  for (let row = 0; row < 5; row++) {
    for (let col = 0; col < 5; col++) {
      const srcCol = col < 3 ? col : 4 - col;
      if (!cells[row * 3 + srcCol]) continue;
      rects.push(
        <rect
          key={`${row}-${col}`}
          x={col * cellSize}
          y={row * cellSize}
          width={cellSize}
          height={cellSize}
          fill={fg}
        />,
      );
    }
  }

  return (
    <svg
      width={size}
      height={size}
      viewBox={`0 0 ${size} ${size}`}
      className={className}
      style={{ borderRadius: '6px', flexShrink: 0, display: 'block' }}
      aria-label={`Agent ${agentId} avatar`}
    >
      <rect width={size} height={size} fill={bg} />
      {rects}
    </svg>
  );
}
