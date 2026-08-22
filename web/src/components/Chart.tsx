// AEROLAB RESILIENCE - interactive engineering charts.
//
// UI-013 to UI-016 ask for the plots. What makes them usable for analysis
// rather than decoration is the readout: moving the pointer across the plot
// puts a crosshair on the nearest sample and shows EVERY series value at that
// instant, with its unit. Clicking pins the instant so a value can be read,
// pointed at, and quoted without the mouse having to stay still.
//
// Everything is plain SVG. A charting library would bring its own tooltip
// conventions and its own idea of what a threshold line looks like, and the one
// thing these plots must get right is that the gate is visible next to the
// statistic it gates.
import { useMemo, useRef, useState } from "react";
import { useLang } from "../i18n";

export interface Series {
  id: string;
  label: string;
  color: string;
  points: Array<[number, number]>;
  dashed?: boolean;
  unit?: string;
}

interface Props {
  series: Series[];
  height?: number;
  yLabel: string;
  xLabel?: string;
  threshold?: { value: number; label: string };
  faultWindow?: { start: number; end: number } | null;
  logY?: boolean;
  allowScaleToggle?: boolean;
  digits?: number;
}

const VIEW_W = 900;
const MARGIN = { top: 14, right: 16, bottom: 34, left: 64 };

export function TimeChart({
  series,
  height = 240,
  yLabel,
  xLabel,
  threshold,
  faultWindow,
  logY = false,
  allowScaleToggle = true,
  digits = 2,
}: Props) {
  const { t, num } = useLang();
  const svgRef = useRef<SVGSVGElement>(null);
  const [hoverT, setHoverT] = useState<number | null>(null);
  const [pinnedT, setPinnedT] = useState<number | null>(null);
  const [useLog, setUseLog] = useState(logY);

  const plotW = VIEW_W - MARGIN.left - MARGIN.right;
  const plotH = height - MARGIN.top - MARGIN.bottom;

  const bounds = useMemo(() => {
    const all = series.flatMap((s) => s.points);
    if (all.length === 0) return null;
    const xMax = Math.max(1, ...all.map((p) => p[0]));
    const values = all.map((p) => p[1]).filter((v) => Number.isFinite(v));
    const rawMax = Math.max(...values, threshold?.value ?? Number.NEGATIVE_INFINITY);
    const positive = values.filter((v) => v > 0);
    const yMin = useLog
      ? Math.max(1e-3, positive.length ? Math.min(...positive) * 0.7 : 1e-3)
      : Math.min(0, ...values);
    const yMax = rawMax > yMin ? rawMax * 1.05 : yMin + 1;
    return { xMin: 0, xMax, yMin, yMax };
  }, [series, threshold, useLog]);

  if (!bounds) {
    return <p className="empty">{t.chart.noData}</p>;
  }

  const { xMin, xMax, yMin, yMax } = bounds;
  const sx = (x: number) => MARGIN.left + ((x - xMin) / (xMax - xMin)) * plotW;
  const sy = (y: number) => {
    if (useLog) {
      const lo = Math.log10(yMin);
      const hi = Math.log10(yMax);
      const v = Math.log10(Math.max(y, yMin));
      return MARGIN.top + plotH - ((v - lo) / (hi - lo)) * plotH;
    }
    return MARGIN.top + plotH - ((y - yMin) / (yMax - yMin)) * plotH;
  };
  const invertX = (px: number) => xMin + ((px - MARGIN.left) / plotW) * (xMax - xMin);

  const yTicks = Array.from({ length: 5 }, (_, i) =>
    useLog
      ? Math.pow(10, Math.log10(yMin) + ((Math.log10(yMax) - Math.log10(yMin)) * i) / 4)
      : yMin + ((yMax - yMin) * i) / 4
  );
  const xTicks = Array.from({ length: 6 }, (_, i) => xMin + ((xMax - xMin) * i) / 5);

  const format = (v: number) => {
    const a = Math.abs(v);
    if (a >= 10000) return v.toExponential(1);
    if (a >= 100) return num(v, 0);
    if (a >= 10) return num(v, 1);
    if (a >= 0.1) return num(v, 2);
    return v.toExponential(1);
  };

  // The instant currently being read: the pinned one wins over the hovered one.
  const activeT = pinnedT ?? hoverT;
  const readout =
    activeT === null
      ? null
      : series
          .map((s) => {
            if (s.points.length === 0) return null;
            // Nearest sample by time. The series are all sampled on the same
            // tick, so a binary search on one would do, but they can be of
            // different lengths while a run is in progress.
            let best = s.points[0];
            let bestDistance = Math.abs(best[0] - activeT);
            for (let i = 1; i < s.points.length; ++i) {
              const d = Math.abs(s.points[i][0] - activeT);
              if (d < bestDistance) {
                bestDistance = d;
                best = s.points[i];
              }
              if (s.points[i][0] > activeT + 1) break;
            }
            return { series: s, t: best[0], value: best[1] };
          })
          .filter((r): r is { series: Series; t: number; value: number } => r !== null);

  const handleMove = (event: React.PointerEvent<SVGSVGElement>) => {
    const svg = svgRef.current;
    if (!svg) return;
    const rect = svg.getBoundingClientRect();
    const px = ((event.clientX - rect.left) / rect.width) * VIEW_W;
    if (px < MARGIN.left || px > MARGIN.left + plotW) {
      setHoverT(null);
      return;
    }
    setHoverT(invertX(px));
  };

  const crosshairT = readout && readout.length > 0 ? readout[0].t : null;

  return (
    <div className="chart-wrap">
      <svg
        ref={svgRef}
        className="chart"
        viewBox={`0 0 ${VIEW_W} ${height}`}
        role="img"
        aria-label={`${yLabel} — ${xLabel ?? t.chart.time}`}
        onPointerMove={handleMove}
        onPointerLeave={() => setHoverT(null)}
        onClick={() => setPinnedT((p) => (p === null ? hoverT : null))}
        style={{ cursor: "crosshair" }}
      >
        {faultWindow && faultWindow.start >= 0 && (
          <>
            <rect
              x={sx(faultWindow.start)}
              y={MARGIN.top}
              width={Math.max(1, sx(Math.min(faultWindow.end, xMax)) - sx(faultWindow.start))}
              height={plotH}
              fill="#e0663d"
              opacity="0.10"
            />
            <line
              x1={sx(faultWindow.start)}
              x2={sx(faultWindow.start)}
              y1={MARGIN.top}
              y2={MARGIN.top + plotH}
              stroke="#e0663d"
              strokeWidth="1.2"
              opacity="0.8"
            />
          </>
        )}

        {yTicks.map((v, i) => (
          <g key={`y${i}`}>
            <line
              x1={MARGIN.left}
              x2={MARGIN.left + plotW}
              y1={sy(v)}
              y2={sy(v)}
              stroke="#1c2735"
            />
            <text
              x={MARGIN.left - 8}
              y={sy(v) + 3.5}
              textAnchor="end"
              fontSize="10.5"
              fill="#7b8da0"
              fontFamily="ui-monospace, monospace"
            >
              {format(v)}
            </text>
          </g>
        ))}
        {xTicks.map((v, i) => (
          <text
            key={`x${i}`}
            x={sx(v)}
            y={height - 11}
            textAnchor="middle"
            fontSize="10.5"
            fill="#7b8da0"
            fontFamily="ui-monospace, monospace"
          >
            {num(v, 0)}
          </text>
        ))}

        {threshold && (
          <g>
            <line
              x1={MARGIN.left}
              x2={MARGIN.left + plotW}
              y1={sy(threshold.value)}
              y2={sy(threshold.value)}
              stroke="#e0663d"
              strokeDasharray="6 4"
            />
            <text
              x={MARGIN.left + plotW - 4}
              y={sy(threshold.value) - 5}
              textAnchor="end"
              fontSize="10.5"
              fill="#e0663d"
              fontFamily="ui-monospace, monospace"
            >
              {threshold.label}
            </text>
          </g>
        )}

        {series.map((s) => {
          if (s.points.length === 0) return null;
          const d = s.points
            .map((p, i) => `${i === 0 ? "M" : "L"}${sx(p[0]).toFixed(1)},${sy(p[1]).toFixed(1)}`)
            .join(" ");
          return (
            <path
              key={s.id}
              d={d}
              fill="none"
              stroke={s.color}
              strokeWidth="1.5"
              strokeLinejoin="round"
              strokeDasharray={s.dashed ? "5 3" : undefined}
            />
          );
        })}

        {crosshairT !== null && (
          <g>
            <line
              x1={sx(crosshairT)}
              x2={sx(crosshairT)}
              y1={MARGIN.top}
              y2={MARGIN.top + plotH}
              stroke={pinnedT !== null ? "#e8c95a" : "#8a9bb0"}
              strokeWidth="1"
              strokeDasharray={pinnedT !== null ? undefined : "3 3"}
            />
            {readout?.map((r) => (
              <circle
                key={r.series.id}
                cx={sx(r.t)}
                cy={sy(r.value)}
                r="3.5"
                fill={r.series.color}
                stroke="#0b0f14"
                strokeWidth="1.2"
              />
            ))}
            <text
              x={sx(crosshairT)}
              y={MARGIN.top - 3}
              textAnchor="middle"
              fontSize="10.5"
              fill={pinnedT !== null ? "#e8c95a" : "#8a9bb0"}
              fontFamily="ui-monospace, monospace"
            >
              {num(crosshairT, 2)} s
            </text>
          </g>
        )}

        <line
          x1={MARGIN.left}
          x2={MARGIN.left}
          y1={MARGIN.top}
          y2={MARGIN.top + plotH}
          stroke="#2b3a4d"
        />
        <line
          x1={MARGIN.left}
          x2={MARGIN.left + plotW}
          y1={MARGIN.top + plotH}
          y2={MARGIN.top + plotH}
          stroke="#2b3a4d"
        />
        <text x={4} y={11} fontSize="10.5" fill="#9fb0c4" fontFamily="ui-monospace, monospace">
          {yLabel}
        </text>
        <text
          x={MARGIN.left + plotW}
          y={height - 11}
          textAnchor="end"
          fontSize="10.5"
          fill="#7b8da0"
          fontFamily="ui-monospace, monospace"
        >
          {xLabel ?? t.chart.time}
        </text>
      </svg>

      <div className="chart-readout">
        {readout && readout.length > 0 ? (
          <>
            <span className="chart-readout-time">
              t = {num(readout[0].t, 2)} s
              {pinnedT !== null && <em> · {t.chart.pinned}</em>}
            </span>
            {readout.map((r) => (
              <span className="chart-readout-item" key={r.series.id}>
                <i style={{ background: r.series.color }} />
                {r.series.label}
                <b>
                  {num(r.value, digits)}
                  {r.series.unit ? ` ${r.series.unit}` : ""}
                </b>
              </span>
            ))}
          </>
        ) : (
          <span className="chart-hint">{t.chart.hoverHint}</span>
        )}
      </div>

      <div className="chart-legend">
        {series.map((s) => (
          <span key={s.id}>
            <i style={{ background: s.color }} />
            {s.label}
          </span>
        ))}
        {faultWindow && faultWindow.start >= 0 && (
          <span>
            <i style={{ background: "#e0663d", opacity: 0.4, height: 8 }} />
            {t.chart.faultActive}
          </span>
        )}
        <span className="chart-actions">
          {pinnedT !== null && (
            <button type="button" onClick={() => setPinnedT(null)}>
              {t.chart.clickToUnpin}
            </button>
          )}
          {allowScaleToggle && (
            <button type="button" onClick={() => setUseLog((v) => !v)}>
              {useLog ? t.chart.logScale : t.chart.linearScale}
            </button>
          )}
        </span>
      </div>
    </div>
  );
}
