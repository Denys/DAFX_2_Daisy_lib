function sp = mdd_sanitize_params(p)
% MDD_SANITIZE_PARAMS  Clamps and fallbacks mirroring DigitalDelayNode (ClampFinite)
%   @ DAFX 73976da, src/pedal_harness/digital_delay_node.hpp. Values stay double here;
%   mdd_process_block casts them to the working precision.
sp = p;
if strcmp(p.Precision, 'single'), sp.Class = 'single'; else, sp.Class = 'double'; end
readers = {'integer', 'linear', 'allpass1', 'lagrange3'};
sp.ReaderId = find(strcmp(p.Reader, readers));
if isempty(sp.ReaderId), error('mdd:reader', 'unknown reader "%s"', p.Reader); end
mins = [1 1 1.5 2];
sp.ReaderMin = mins(sp.ReaderId);
sp.MaxDelaySamples = round(p.MaxDelayMs * p.Fs / 1000);
if ~any(p.Config == [1 2 3]), sp.Config = 1; end
guard = 0.49 * p.Fs;
sp.Dry = clamp_finite(p.Dry, 1, 0, 2);
sp.Wet = clamp_finite(p.Wet, 0, 0, 2);
g = p.SeriesGains; g(~isfinite(g)) = 1; sp.SeriesGains = g;
sp.ParallelNorm = clamp_finite(p.ParallelNorm, 0.5, 0, 1);
for e = 1:2
  E = p.E(e);
  sp.E(e).Feedback = clamp_finite(E.Feedback, 0, -0.999, 0.999);
  sp.E(e).InputSend = clamp_finite(E.InputSend, 1, 0, 1);
  sp.E(e).LowCutHz = clamp_finite(E.LowCutHz, 0, 0, guard);
  sp.E(e).HighCutHz = clamp_finite(E.HighCutHz, guard, 0, guard);
  sp.E(e).DelaySamples = clamp_finite(E.DelaySamples, 1, sp.ReaderMin, sp.MaxDelaySamples);
end
end

function v = clamp_finite(v, fallback, lo, hi)
if ~isfinite(v), v = fallback; end
v = min(max(v, lo), hi);
end
