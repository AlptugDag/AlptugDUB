import React, { useCallback, useRef } from 'react';
import { clamp } from '../dsp/characters';

interface RotaryKnobProps {
  id: string;
  label: string;
  value: number;
  min: number;
  max: number;
  step?: number;
  defaultValue: number;
  formatValue: (v: number) => string;
  onChange: (next: number) => void;
  bipolar?: boolean;
  size?: 'md' | 'lg';
  accent?: 'amber' | 'cyan';
  disabled?: boolean;
  tooltip?: string;
}

const START_ANGLE = -135;
const END_ANGLE = 135;
const SWEEP = END_ANGLE - START_ANGLE;

function polarToCartesian(cx: number, cy: number, r: number, angleDeg: number) {
  const rad = ((angleDeg - 90) * Math.PI) / 180;
  return { x: cx + r * Math.cos(rad), y: cy + r * Math.sin(rad) };
}

function describeArc(cx: number, cy: number, r: number, startAngle: number, endAngle: number): string {
  if (Math.abs(endAngle - startAngle) < 0.5) return '';
  const lo = Math.min(startAngle, endAngle);
  const hi = Math.max(startAngle, endAngle);
  const start = polarToCartesian(cx, cy, r, hi);
  const end = polarToCartesian(cx, cy, r, lo);
  const largeArcFlag = hi - lo <= 180 ? '0' : '1';
  return `M ${start.x.toFixed(2)} ${start.y.toFixed(2)} A ${r} ${r} 0 ${largeArcFlag} 0 ${end.x.toFixed(2)} ${end.y.toFixed(2)}`;
}

export const RotaryKnob: React.FC<RotaryKnobProps> = ({
  id,
  label,
  value,
  min,
  max,
  step = 0.01,
  defaultValue,
  formatValue,
  onChange,
  bipolar = false,
  size = 'md',
  accent = 'amber',
  disabled = false,
  tooltip,
}) => {
  const dragRef = useRef<{ startY: number; startX: number; startVal: number } | null>(null);
  const norm = clamp((value - min) / (max - min), 0, 1);
  const angle = START_ANGLE + norm * SWEEP;
  const zeroNorm = bipolar ? clamp((0 - min) / (max - min), 0, 1) : 0;
  const zeroAngle = START_ANGLE + zeroNorm * SWEEP;

  const commit = useCallback(
    (raw: number) => {
      const snapped = Math.round(raw / step) * step;
      const clamped = clamp(Number(snapped.toFixed(4)), min, max);
      onChange(clamped);
    },
    [min, max, step, onChange]
  );

  const handlePointerDown = (e: React.PointerEvent<HTMLDivElement>) => {
    if (disabled) return;
    e.currentTarget.setPointerCapture(e.pointerId);
    dragRef.current = { startY: e.clientY, startX: e.clientX, startVal: value };
  };

  const handlePointerMove = (e: React.PointerEvent<HTMLDivElement>) => {
    if (!dragRef.current || disabled) return;
    const sensitivity = e.shiftKey ? 540 : size === 'lg' ? 230 : 185;
    const dy = dragRef.current.startY - e.clientY;
    const dx = (e.clientX - dragRef.current.startX) * 0.35;
    const deltaNorm = (dy + dx) / sensitivity;
    commit(dragRef.current.startVal + deltaNorm * (max - min));
  };

  const handlePointerUp = (e: React.PointerEvent<HTMLDivElement>) => {
    if (dragRef.current) {
      e.currentTarget.releasePointerCapture(e.pointerId);
      dragRef.current = null;
    }
  };

  const handleWheel = (e: React.WheelEvent<HTMLDivElement>) => {
    if (disabled) return;
    e.preventDefault();
    const dir = e.deltaY < 0 ? 1 : -1;
    const mult = e.shiftKey ? 1 : Math.max(1, Math.round((max - min) / (step * 45)));
    commit(value + dir * step * mult);
  };

  const handleKeyDown = (e: React.KeyboardEvent<HTMLDivElement>) => {
    if (disabled) return;
    const coarse = Math.max(step, (max - min) / 36);
    const delta = e.shiftKey ? step : coarse;
    if (e.key === 'ArrowUp' || e.key === 'ArrowRight') {
      e.preventDefault();
      commit(value + delta);
    } else if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') {
      e.preventDefault();
      commit(value - delta);
    } else if (e.key === 'Home') {
      e.preventDefault();
      commit(min);
    } else if (e.key === 'End') {
      e.preventDefault();
      commit(max);
    } else if (e.key === 'Backspace' || e.key === 'Delete' || e.key === '0') {
      e.preventDefault();
      commit(defaultValue);
    }
  };

  const dim = size === 'lg' ? 128 : 94;
  const cx = dim / 2;
  const cy = dim / 2;
  const trackR = size === 'lg' ? 54 : 39;
  const bodyR = size === 'lg' ? 43 : 30;
  const tickCount = size === 'lg' ? 25 : 19;

  const activeStart = bipolar ? zeroAngle : START_ANGLE;
  const activeEnd = angle;
  const arcPath = describeArc(cx, cy, trackR, activeStart, activeEnd);
  const bgArcPath = describeArc(cx, cy, trackR, START_ANGLE, END_ANGLE);
  const ptrInner = polarToCartesian(cx, cy, bodyR * 0.34, angle);
  const ptrOuter = polarToCartesian(cx, cy, bodyR * 0.84, angle);

  const gradId = `knob-arc-${id}`;
  const bodyGradId = `knob-body-${id}`;

  return (
    <div
      className={`flex flex-col items-center select-none ${disabled ? 'opacity-50' : ''}`}
      title={tooltip}
    >
      <span
        className={`text-center font-semibold tracking-wider text-slate-400 whitespace-nowrap ${
          size === 'lg' ? 'text-xs mb-1.5' : 'text-[11px] mb-1'
        }`}
      >
        {label}
      </span>

      <div
        role="slider"
        tabIndex={disabled ? -1 : 0}
        aria-label={label}
        aria-valuemin={min}
        aria-valuemax={max}
        aria-valuenow={Number(value.toFixed(2))}
        aria-valuetext={formatValue(value)}
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onPointerUp={handlePointerUp}
        onDoubleClick={() => !disabled && commit(defaultValue)}
        onWheel={handleWheel}
        onKeyDown={handleKeyDown}
        className="relative rounded-full focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-amber-400/70 touch-none"
        style={{ width: dim, height: dim }}
      >
        <svg width={dim} height={dim} viewBox={`0 0 ${dim} ${dim}`} className="overflow-visible">
          <defs>
            <linearGradient id={gradId} x1="0%" y1="100%" x2="100%" y2="0%">
              {accent === 'cyan' ? (
                <>
                  <stop offset="0%" stopColor="#06B6D4" />
                  <stop offset="100%" stopColor="#67E8F9" />
                </>
              ) : (
                <>
                  <stop offset="0%" stopColor="#F59E0B" />
                  <stop offset="55%" stopColor="#FBBF24" />
                  <stop offset="100%" stopColor="#22D3EE" />
                </>
              )}
            </linearGradient>
            <radialGradient id={bodyGradId} cx="35%" cy="28%" r="72%">
              <stop offset="0%" stopColor="#2A3242" />
              <stop offset="65%" stopColor="#171C26" />
              <stop offset="100%" stopColor="#0D1118" />
            </radialGradient>
          </defs>

          {/* Calibration tick marks */}
          {Array.from({ length: tickCount }).map((_, idx) => {
            const t = idx / (tickCount - 1);
            const a = START_ANGLE + t * SWEEP;
            const p1 = polarToCartesian(cx, cy, trackR + 4, a);
            const p2 = polarToCartesian(cx, cy, trackR + 8, a);
            const isCenter = bipolar && idx === Math.floor(tickCount / 2);
            return (
              <line
                key={idx}
                x1={p1.x}
                y1={p1.y}
                x2={p2.x}
                y2={p2.y}
                stroke={isCenter ? '#F59E0B' : 'rgba(148,163,184,0.28)'}
                strokeWidth={isCenter ? 1.8 : 1.1}
                strokeLinecap="round"
              />
            );
          })}

          {/* Track background arc */}
          <path
            d={bgArcPath}
            fill="none"
            stroke="#1E2634"
            strokeWidth={size === 'lg' ? 4.5 : 3.5}
            strokeLinecap="round"
          />

          {/* Active illuminated arc */}
          {arcPath && (
            <path
              d={arcPath}
              fill="none"
              stroke={`url(#${gradId})`}
              strokeWidth={size === 'lg' ? 4.8 : 3.8}
              strokeLinecap="round"
            />
          )}

          {/* Machined knurled outer ring */}
          <circle
            cx={cx}
            cy={cy}
            r={bodyR + 2.5}
            fill="#0A0E15"
            stroke="rgba(255,255,255,0.08)"
            strokeWidth={1}
          />
          <circle
            cx={cx}
            cy={cy}
            r={bodyR}
            fill={`url(#${bodyGradId})`}
            stroke="rgba(255,255,255,0.14)"
            strokeWidth={1.1}
          />

          {/* Indicator needle */}
          <line
            x1={ptrInner.x}
            y1={ptrInner.y}
            x2={ptrOuter.x}
            y2={ptrOuter.y}
            stroke={size === 'lg' ? '#FBBF24' : '#E2E8F0'}
            strokeWidth={size === 'lg' ? 2.8 : 2.2}
            strokeLinecap="round"
          />
        </svg>
      </div>

      <div
        className={`mt-1.5 rounded border border-white/10 bg-[#0A0D13] px-2.5 py-0.5 font-mono tabular-nums text-center whitespace-nowrap ${
          size === 'lg'
            ? 'min-w-[84px] text-xs font-semibold text-amber-300'
            : 'min-w-[68px] text-[11px] font-medium text-slate-200'
        }`}
      >
        {formatValue(value)}
      </div>
    </div>
  );
};
