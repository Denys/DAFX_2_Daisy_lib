function p = mdd_controls_to_params(ctl, p)
% MDD_CONTROLS_TO_PARAMS  Contract v1 sections 5-6: front-panel controls -> engine parameters.
%   Controls in [0,1] except Config (1..3) and RatioIndex (1..7). The kernel keeps its own
%   engine-level clamps; this layer only produces values inside them.
ratios = [1/4 1/3 3/8 1/2 2/3 3/4 1];
cfg = ctl.Config;
if ~any(cfg == [1 2 3]), cfg = 1; end
t = unit(ctl.Time, 0.6); fb = unit(ctl.Feedback, 0.5); col = unit(ctl.Color, 0); mix = unit(ctl.Mix, 0.5);
ri = ctl.RatioIndex;
if ~isfinite(ri), ri = 6; end
ri = min(max(round(ri), 1), 7);

D1 = round(20 * (2500 / 20) ^ t * p.Fs / 1000);       % TIME: 20 ms ... 2.5 s, log taper
D2 = round(ratios(ri) * D1);                          % SHIFT+TIME: E2 = ratio x D1
k = 0.95 * [fb fb];                                   % FEEDBACK: knob -> k in [0, 0.95], linked
if isfinite(ctl.FeedbackE2), k(2) = 0.95 * unit(ctl.FeedbackE2, fb); end
if cfg == 2
  f = 1 - sqrt(1 - k);                                % contract 6: series compensation
else
  f = k;
end
guard = 0.49 * p.Fs;
if col == 0, hc = guard; else, hc = guard * (2000 / guard) ^ col; end   % COLOR: LP off ... 2 kHz

p.Ctl = ctl; p.Config = cfg; p.Dry = 1; p.Wet = mix;
D = [D1 D2];
for e = 1:2
  p.E(e).DelaySamples = D(e); p.E(e).Feedback = f(e); p.E(e).InputSend = 1;
  p.E(e).LowCutHz = 40; p.E(e).HighCutHz = hc;          % HP fixed at 40 Hz
end
end

function v = unit(v, fallback)
if ~isfinite(v), v = fallback; end
v = min(max(v, 0), 1);
end
