// AEROLAB RESILIENCE - engineering charts (UI-013, UI-014, UI-015, UI-016).
//
// Plain SVG, no charting dependency. Every axis carries its unit, every plot
// carries the threshold it is judged against, and the fault window is shaded so
// a reader can see at a glance whether an excursion happened while a fault was
// active or not.
import type { ReactNode } from "react";

export interface Series {
  id: string;
  label: string;
  color: string;
  points: Array<[number, number]>;
  dashed?: boolean;
}

interface Props {
  series: Series[];
  width?: number;
  height?: number;
  yLabel: string;
  xLabel?: string;
  threshold?: { value: number; label: string };
  faultWindow?: { start: number; end: number } | null;
  logY?: boolean;
  children?: ReactNode;
}

export function TimeChart({
  series,
  width = 760,
  height = 220,
  yLabel,
  xLabel = "time (s)",
  threshold,
  faultWindow,
  logY = false,
}: Props) {
  const margin = { top: 10, right: 14, bottom: 30, left: 58 };
  const plotW = width - margin.left - margin.right;
  const plotH = height - margin.top - margin.bottom;

  const all = series.flatMap((s) => s.points);
  if (all.length === 0) {
    return (
      <p className="empty">No data yet. Start a run to populate this plot.</p>
    );
  }

  const xMin = 0;
  const xMax = Math.max(1, ...all.map((p) => p[0]));
  const rawMax = Math.max(...all.map((p) => p[1]), threshold?.value ?? 0);
  const rawMin = Math.min(...all.map((p) => p[1]), 0);
  const yMin = logY ? Math.max(1e-3, Math.min(...all.map((p) => Math.abs(p[1]) || 1e-3))) : rawMin;
  const yMax = rawMax > yMin ? rawMax : yMin + 1;

  const sx = (x: number) => margin.left + ((x - xMin) / (xMax - xMin)) * plotW;
  const sy = (y: number) => {
    if (logY) {
      const lo = Math.log10(yMin);
      const hi = Math.log10(yMax);
      const v = Math.log10(Math.max(Math.abs(y), yMin));
      return margin.top + plotH - ((v - lo) / (hi - lo)) * plotH;
    }
    return margin.top + plotH - ((y - yMin) / (yMax - yMin)) * plotH;
  };

  const ticksY = 4;
  const yTickValues = Array.from({ length: ticksY + 1 }, (_, i) =>
    logY
      ? Math.pow(10, Math.log10(yMin) + ((Math.log10(yMax) - Math.log10(yMin)) * i) / ticksY)
      : yMin + ((yMax - yMin) * i) / ticksY
  );
  const ticksX = 5;
  const xTickValues = Array.from({ length: ticksX + 1 }, (_, i) => xMin + ((xMax - xMin) * i) / ticksX);

  const format = (v: number) => {
    const a = Math.abs(v);
    if (a >= 1000) return v.toExponential(1);
    if (a >= 10) return v.toFixed(0);
    if (a >= 1) return v.toFixed(1);
    return v.toFixed(2);
  };

  return (
    <div>
      <svg className="chart" viewBox={`0 0 ${width} ${height}`} role="img" aria-label={`${yLabel} against ${xLabel}`}>
        {faultWindow && faultWindow.start >= 0 && (
          <rect
            x={sx(faultWindow.start)}
            y={margin.top}
            width={Math.max(1, sx(Math.min(faultWindow.end, xMax)) - sx(faultWindow.start))}
            height={plotH}
            fill="#e0663d"
            opacity="0.10"
          />
        )}

        {yTickValues.map((v, i) => (
          <g key={`y${i}`}>
            <line x1={margin.left} x2={margin.left + plotW} y1={sy(v)} y2={sy(v)} stroke="#1c2735" />
            <text x={margin.left - 7} y={sy(v) + 3.5} textAnchor="end" fontSize="10" fill="#61738a" fontFamily="ui-monospace, monospace">
              {format(v)}
            </text>
          </g>
        ))}
        {xTickValues.map((v, i) => (
          <text key={`x${i}`} x={sx(v)} y={height - 10} textAnchor="middle" fontSize="10" fill="#61738a" fontFamily="ui-monospace, monospace">
            {v.toFixed(0)}
          </text>
        ))}

        {threshold && (
          <g>
            <line
              x1={margin.left}
              x2={margin.left + plotW}
              y1={sy(threshold.value)}
              y2={sy(threshold.value)}
              stroke="#e0663d"
              strokeDasharray="5 4"
            />
            <text x={margin.left + plotW - 3} y={sy(threshold.value) - 4} textAnchor="end" fontSize="10" fill="#e0663d" fontFamily="ui-monospace, monospace">
              {threshold.label}
            </text>
          </g>
        )}

        {series.map((s) => {
          if (s.points.length === 0) return null;
          const d = s.points.map((p, i) => `${i === 0 ? "M" : "L"}${sx(p[0]).toFixed(1)},${sy(p[1]).toFixed(1)}`).join(" ");
          return (
            <path
              key={s.id}
              d={d}
              fill="none"
              stroke={s.color}
              strokeWidth="1.4"
              strokeDasharray={s.dashed ? "4 3" : undefined}
            />
          );
        })}

        <line x1={margin.left} x2={margin.left} y1={margin.top} y2={margin.top + plotH} stroke="#253243" />
        <line x1={margin.left} x2={margin.left + plotW} y1={margin.top + plotH} y2={margin.top + plotH} stroke="#253243" />
        <text x={4} y={12} fontSize="10" fill="#8a9bb0" fontFamily="ui-monospace, monospace">
          {yLabel}
        </text>
      </svg>
      <div className="chart-legend">
        {series.map((s) => (
          <span key={s.id}>
            <i style={{ background: s.color }} />
            {s.label}
          </span>
        ))}
        {faultWindow && faultWindow.start >= 0 && (
          <span>
            <i style={{ background: "#e0663d", opacity: 0.35, height: 8 }} />
            fault active
          </span>
        )}
      </div>
    </div>
  );
}
