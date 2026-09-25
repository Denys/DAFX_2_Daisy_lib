function [y, st, taps] = mdd_process_block(x, p, st)
% MDD_PROCESS_BLOCK  Pure kernel of the mono-first DIGI graph (ADR-0019, contract v1).
%   [y, st] = mdd_process_block(x, p, st) processes column x under ONE parameter snapshot
%   (all controls latched at call entry; coefficients computed once). Call it once per
%   block to automate parameters; a config change takes effect at the call boundary and
%   never clears histories. Optional taps (N x 2) returns each engine's delayed sample.
%
%   Per-engine law mirrors DigitalDelayNode::Process @ DAFX 73976da,
%   src/pedal_harness/digital_delay_node.hpp:73-143 (filters 180-227), in mono:
%     tap = h[D]; cond = LP(HP(tap)); h <- send*in + fb*cond
%   Routing (contract 4): in1 = x; in2 = 0 | tap1 | x;  y = Dry*x + Wet*(g1*tap1 + g2*tap2).
sp = mdd_sanitize_params(p);
cls = sp.Class;
x = cast(x(:), cls); N = numel(x);
y = zeros(N, 1, cls);
want_taps = nargout > 2;
if want_taps, taps = zeros(N, 2, cls); end

% ---- snapshot: coefficients and gains, in the node's operation order
two = cast(2, cls); pic = cast(pi, cls); fsc = cast(sp.Fs, cls);
guard = fsc * cast(0.49, cls);
hpOn = false(1, 2); lpOn = false(1, 2);
hpP = zeros(1, 2, cls); lpA = ones(1, 2, cls); fb = zeros(1, 2, cls); send = zeros(1, 2, cls);
Dt = zeros(1, 2);
for e = 1:2
  lo = cast(sp.E(e).LowCutHz, cls); hi = cast(sp.E(e).HighCutHz, cls);
  hpOn(e) = lo > 0; lpOn(e) = hi < guard;
  if hpOn(e), hpP(e) = exp(-two * pic * lo / fsc); end
  if lpOn(e), lpA(e) = 1 - exp(-two * pic * hi / fsc); end
  fb(e) = cast(sp.E(e).Feedback, cls); send(e) = cast(sp.E(e).InputSend, cls);
  Dt(e) = sp.E(e).DelaySamples;
end
dry = cast(sp.Dry, cls); wet = cast(sp.Wet, cls);
G = [1 0; sp.SeriesGains; sp.ParallelNorm * [1 1]];
g1 = cast(G(sp.Config, 1), cls); g2 = cast(G(sp.Config, 2), cls);
cfg = sp.Config; rid = sp.ReaderId;
feed_write = strcmp(sp.SeriesFeedPoint, 'write');
run_e2 = ~(cfg == 1 && strcmp(sp.SingleE2Input, 'hold'));
if sp.TimeSmoothMs > 0, alpha = 1 - exp(-1 / (sp.TimeSmoothMs / 1000 * sp.Fs)); else, alpha = 1; end

% ---- unpack state
buf = st.buf; w = st.w; cap = st.cap; m = cap - 1;
hx1 = st.hp_x1; hy1 = st.hp_y1; ly = st.lp_y; ay1 = st.ap_y1; Dc = st.Dcur;
Dc(isnan(Dc)) = Dt(isnan(Dc));
nf = st.diag.non_finite_samples; clip = st.diag.clipped_samples;
peak = cast(st.diag.max_abs_sample, cls);
zero = zeros(1, 1, cls); one = ones(1, 1, cls);
t = zeros(1, 2, cls); wr = zeros(1, 2, cls);

for n = 1:N
  xin = x(n);
  if ~isfinite(xin), xin = zero; nf = nf + 1; end        % once at ingress
  if alpha == 1, Dc = Dt; else, Dc = Dc + alpha * (Dt - Dc); end
  for e = 1:2
    if e == 1
      in = xin;
    else
      if ~run_e2, t(2) = zero; break; end
      if cfg == 1, in = zero;
      elseif cfg == 2
        if feed_write, in = wr(1); else, in = t(1); end
      else, in = xin;
      end
    end
    [tap, ay1(e)] = mdd_read(buf, e, w, cap, Dc(e), rid, ay1(e));
    if ~isfinite(tap), tap = zero; nf = nf + 1; end
    c = tap;
    if hpOn(e)
      o = hpP(e) * (hy1(e) + c - hx1(e)); hx1(e) = c; hy1(e) = o; c = o;
    end
    if lpOn(e)
      ly(e) = ly(e) + lpA(e) * (c - ly(e)); c = ly(e);
    end
    v = send(e) * in + fb(e) * c;
    if ~isfinite(v), v = zero; nf = nf + 1; end
    buf(w, e) = v; t(e) = tap; wr(e) = v;
  end
  yo = dry * xin + wet * (g1 * t(1) + g2 * t(2));
  if ~isfinite(yo), yo = zero; nf = nf + 1; end
  a = abs(yo);
  if a > one, clip = clip + 1; end
  if a > peak, peak = a; end
  y(n) = yo;
  if want_taps, taps(n, :) = t; end
  w = bitand(w, m) + 1;
end

% ---- pack state
st.buf = buf; st.w = w; st.hp_x1 = hx1; st.hp_y1 = hy1; st.lp_y = ly; st.ap_y1 = ay1; st.Dcur = Dc;
st.diag.non_finite_samples = nf; st.diag.clipped_samples = clip; st.diag.max_abs_sample = double(peak);
st.diag.processed_blocks = st.diag.processed_blocks + ceil(N / sp.BlockSize);
st.diag.processed_samples = st.diag.processed_samples + N;
end
