/**
 * Identicon — deterministic SVG avatar generated from an agent_id.
 * 5×5 symmetric grid (mirrored left/right) with hashed hsl color.
 * No external dependencies.
 */

function hash(n: number): number {
  let h = (n ^ 0xdeadbeef) >>> 0;
  h = (Math.imul(h ^ (h >>> 16), 0x45d9f3b)) >>> 0;
  h = (Math.imul(h ^ (h >>> 16), 0x45d9f3b)) >>> 0;
  return (h ^ (h >>> 16)) >>> 0;
}

function identiconData(agentId: number): { cells: boolean[][]; color: string } {
  const h0 = hash(agentId);
  const h1 = hash(agentId + 1000);
  const h2 = hash(agentId + 2000);
  const hue = h0 % 360;
  const sat = 45 + (h1 % 30);
  const lit = 50 + (h2 % 15);
  const color = `hsl(${hue},${sat}%,${lit}%)`;

  const ROWS = 5;
  const HALF = 3;
  const bits = hash(agentId + 3000);
  const cells: boolean[][] = [];
  for (let r = 0; r < ROWS; r++) {
    const row: boolean[] = [];
    for (let c = 0; c < HALF; c++) {
      const idx = r * HALF + c;
      const filled = idx < 32 ? ((bits >>> idx) & 1) === 1 : hash(agentId + idx) % 2 === 1;
      row.push(filled);
    }
    row.push(row[1]); // mirror: col 3 = col 1
    row.push(row[0]); // mirror: col 4 = col 0
    cells.push(row);
  }
  return { cells, color };
}

interface Props {
  agentId: number;
  size?: number;
  className?: string;
}

export function Identicon({ agentId, size = 32, className }: Props) {
  const { cells, color } = identiconData(agentId);
  const COLS = 5;
  const pad = Math.round(size * 0.1);
  const cellSize = (size - pad * 2) / COLS;

  return (
    <svg
      width={size}
      height={size}
      viewBox={`0 0 ${size} ${size}`}
      className={className}
      style={{ borderRadius: '50%', background: '#21262d', flexShrink: 0, marginTop: 2 }}
      aria-label={`Agent ${agentId} avatar`}
    >
      {cells.map((row, r) =>
        row.map((filled, c) =>
          filled ? (
            <rect
              key={`${r}-${c}`}
              x={pad + c * cellSize}
              y={pad + r * cellSize}
              width={cellSize - 1}
              height={cellSize - 1}
              rx={1}
              fill={color}
            />
          ) : null,
        ),
      )}
    </svg>
  );
}
