/**
 * Identicon — deterministic geometric avatar from agent_id.
 * No dependencies. Pure SVG, ~1 KB rendered.
 *
 * Algorithm:
 *   seed = agent_id  (integer)
 *   - bg colour : HSL derived from seed
 *   - 3 shapes  : circle, rect, polygon — each toggled/positioned by different bit groups
 */

import React from 'react';

interface IdenticonProps {
  agentId: number;
  size?: number;        // px, default 32
  className?: string;
  style?: React.CSSProperties;
}

/** Simple deterministic hash — mixes seed through several rounds */
function hash(n: number): number {
  let h = (n ^ 0xdeadbeef) >>> 0;
  h = Math.imul(h ^ (h >>> 16), 0x45d9f3b) >>> 0;
  h = Math.imul(h ^ (h >>> 16), 0x45d9f3b) >>> 0;
  return (h ^ (h >>> 16)) >>> 0;
}

function deriveColors(id: number): [string, string, string] {
  const h1 = hash(id);
  const h2 = hash(id + 1337);
  const h3 = hash(id + 7919);

  const hue1 = h1 % 360;
  const hue2 = (hue1 + 120 + (h2 % 60)) % 360;
  const hue3 = (hue1 + 240 + (h3 % 60)) % 360;

  const bg  = `hsl(${hue1},55%,28%)`;
  const fg1 = `hsl(${hue2},80%,65%)`;
  const fg2 = `hsl(${hue3},70%,72%)`;
  return [bg, fg1, fg2];
}

export const Identicon: React.FC<IdenticonProps> = ({ agentId, size = 32, className, style }) => {
  const v = hash(agentId);
  const [bg, fg1, fg2] = deriveColors(agentId);

  // Normalise coordinates to a 100×100 viewBox
  const S = 100;
  const cx = 50;
  const cy = 50;

  // Shape 1 — large circle, offset by bits 0-7
  const r1 = 22 + (v & 0xf);                       // 22-38
  const x1 = 20 + ((v >> 4) & 0x1f);              // 20-51
  const y1 = 20 + ((v >> 9) & 0x1f);              // 20-51

  // Shape 2 — rotated rect
  const rw = 18 + ((v >> 14) & 0x1f);             // 18-49
  const rh = 10 + ((v >> 19) & 0x0f);             // 10-25
  const rx2 = 15 + ((v >> 23) & 0x1f);            // 15-46
  const ry2 = 15 + ((v >> 28) & 0x1f);            // 15-46
  const rot = ((v >> 16) & 0x7f) * 2;             // 0-254 degrees

  // Shape 3 — triangle / polygon
  const v2 = hash(agentId ^ 0xf00d);
  const px = (n: number, s = S) => 10 + (n & 0x3f) % (s - 20);
  const pts = [
    [px(v2), px(v2 >> 6)],
    [px(v2 >> 12), px(v2 >> 18)],
    [px(v2 >> 24), px(hash(v2) & 0x3f)],
  ].map(([x, y]) => `${x},${y}`).join(' ');

  return (
    <svg
      width={size}
      height={size}
      viewBox={`0 0 ${S} ${S}`}
      className={className}
      style={{ borderRadius: '50%', flexShrink: 0, ...style }}
      aria-label={`agent-${agentId}`}
    >
      {/* Background */}
      <rect width={S} height={S} fill={bg} rx={S / 2} />

      {/* Shape 1 — circle */}
      <circle cx={x1} cy={y1} r={r1} fill={fg1} opacity={0.75} />

      {/* Shape 2 — rotated rect */}
      <rect
        x={rx2} y={ry2}
        width={rw} height={rh}
        fill={fg2}
        opacity={0.8}
        transform={`rotate(${rot} ${cx} ${cy})`}
        rx={3}
      />

      {/* Shape 3 — triangle */}
      <polygon points={pts} fill={fg1} opacity={0.6} />

      {/* Subtle inner ring */}
      <circle cx={cx} cy={cy} r={S / 2 - 2} fill="none" stroke={fg2} strokeWidth={1.5} opacity={0.3} />
    </svg>
  );
};

export default Identicon;
