function run_mdd_tests(mode)
% RUN_MDD_TESTS  Test suite for the mono-first DIGI model (Octave >= 8 and MATLAB).
%   run_mdd_tests          quick mode (T13 soak scaled by 0.1)
%   run_mdd_tests('full')  T13 at the contract length (50 s noise + 10 s silence)
% Prints one line per test and ends with "MDD_TESTS PASS p/n" or throws.
if nargin < 1, mode = 'quick'; end
here = fileparts(mfilename('fullpath'));
addpath(fileparts(here));

tests = {@t1_oracle, @t2_markers, @t3_integer_readers, @t4_fractional_readers, ...
         @t5_block_size, @t6_reset, @t7_sanitation, @t8_routing, @t9_cpp_parity, ...
         @t10_contract_markers, @t11_controls, @t12_series_buildup, @t13_soak, ...
         @t14_system_object};
n = numel(tests); passed = 0; skipped = 0; failed = {};
for k = 1:n
  name = func2str(tests{k});
  try
    [status, msg] = tests{k}(mode);
  catch err
    status = 0; msg = ['ERROR ' err.message];
  end
  switch status
    case 1,  passed = passed + 1;  tag = 'PASS';
    case -1, skipped = skipped + 1; tag = 'SKIP';
    otherwise, failed{end+1} = name; tag = 'FAIL'; %#ok<AGROW>
  end
  fprintf('%-24s %s  %s\n', name, tag, msg);
end
if ~isempty(failed)
  error('MDD_TESTS FAIL %d/%d: %s', numel(failed), n, strjoin(failed, ', '));
end
fprintf('MDD_TESTS PASS %d/%d (%d SKIP)\n', passed, n, skipped);
end

% ---------------------------------------------------------------- helpers
function p = base_params(cfg, filters_on, precision)
if nargin < 3, precision = 'double'; end
p = mdd_default_params();
p.Precision = precision; p.Reader = 'integer'; p.Config = cfg;
p.Dry = 1; p.Wet = 0.7; p.TimeSmoothMs = 0;
p.E(1).DelaySamples = 1230; p.E(2).DelaySamples = 770;
p.E(1).Feedback = 0.6;      p.E(2).Feedback = 0.45;
for e = 1:2
  p.E(e).InputSend = 1;
  if filters_on
    p.E(e).LowCutHz = 80; p.E(e).HighCutHz = 4000;
  else
    p.E(e).LowCutHz = 0;  p.E(e).HighCutHz = 0.49 * p.Fs;
  end
end
end

function [y, st, taps] = run_kernel(x, p)
st = mdd_init_state(p);
[y, st, taps] = mdd_process_block(x, p, st);
end

function x = impulse(N)
x = zeros(N, 1); x(1) = 1;
end

function x = seeded_noise(N, rms_level)
if exist('OCTAVE_VERSION', 'builtin')
  randn('seed', 1); %#ok<RAND>
else
  rng(1);
end
x = randn(N, 1) * rms_level;
end

function [idx, amp] = first_nonzero(y, count)
idx = find(abs(y) > 1e-15);
idx = idx(1:min(count, numel(idx)));
amp = y(idx);
idx = idx - 1;                       % zero-based sample index, as in the contract
end

function r = rms_of(v)
r = sqrt(mean(v .^ 2));
end

% ---------------------------------------------------------------- tests
function [s, m] = t1_oracle(~)
N = 20000; worst = 0;
for cfg = 1:3
  for filt = [false true]
    p = base_params(cfg, filt);
    y = run_kernel(impulse(N), p);
    [b, a] = mdd_transfer_oracle(p);
    worst = max(worst, max(abs(y - filter(b, a, impulse(N)))));
  end
end
s = worst <= 1e-12; m = sprintf('max|d| = %.3g (<= 1e-12)', worst);
end

function [s, m] = t2_markers(~)
N = 20000; D = [1230 770]; f = [0.6 0.45]; worst = 0; idx_ok = true;
for cfg = 1:3
  p = base_params(cfg, false); p.Dry = 0; p.Wet = 1;
  y = run_kernel(impulse(N), p);
  e = expected_markers(cfg, D, f, N, p);
  got = find(abs(y) > 1e-15); want = find(abs(e) > 1e-15);
  idx_ok = idx_ok && isequal(got, want);
  worst = max(worst, max(abs(y - e)));
end
s = idx_ok && worst <= 1e-12;
m = sprintf('indices %s, max|d| = %.3g', tf(idx_ok), worst);
end

function e = expected_markers(cfg, D, f, N, p)
% Independent enumeration of echo trains, filters off, coincident terms summed.
t1 = zeros(N, 1); t2 = zeros(N, 1);
for a = 1:ceil(N / D(1))
  n = a * D(1); if n < N, t1(n + 1) = t1(n + 1) + f(1) ^ (a - 1); end
end
for a = 1:ceil(N / D(2))
  for b = 0:ceil(N / D(1))
    if cfg == 2      % series: E2 hears E1's echoes
      if b == 0, continue; end
      n = b * D(1) + a * D(2); amp = f(1) ^ (b - 1) * f(2) ^ (a - 1);
    elseif cfg == 3  % parallel: E2 hears x
      if b > 0, break; end
      n = a * D(2); amp = f(2) ^ (a - 1);
    else
      break;
    end
    if n < N, t2(n + 1) = t2(n + 1) + amp; end
  end
end
g = [1 0; p.SeriesGains; p.ParallelNorm * [1 1]];
e = p.Wet * (g(cfg, 1) * t1 + g(cfg, 2) * t2);
end

function [s, m] = t3_integer_readers(~)
readers = {'linear', 'allpass1', 'lagrange3'};
p0 = base_params(1, true); maxD = round(p0.MaxDelayMs * p0.Fs / 1000);
worst = 0;
for Dk = [2 17 maxD]
  p = p0; p.E(1).DelaySamples = Dk; p.E(1).Feedback = 0.5;
  x = seeded_noise(Dk * 2 + 500, 0.1);
  yi = run_kernel(x, p);
  for r = 1:numel(readers)
    p.Reader = readers{r};
    worst = max(worst, max(abs(run_kernel(x, p) - yi)));
  end
  p.Reader = 'integer';
end
s = worst <= 1e-12; m = sprintf('max|d| vs integer = %.3g at D in {2,17,max}', worst);
end

function [s, m] = t4_fractional_readers(~)
readers = {'linear', 'allpass1', 'lagrange3'}; w = 2 * pi * 100 / 48000;
dc_worst = 0; pd = zeros(3, 9);
for r = 1:3
  for q = 1:9
    p = base_params(1, false); p.Reader = readers{r}; p.Dry = 0; p.Wet = 1;
    p.E(1).Feedback = 0; p.E(1).DelaySamples = 10 + q / 10;
    ydc = run_kernel(ones(4000, 1), p);
    dc_worst = max(dc_worst, abs(ydc(end) - 1));
    h = run_kernel(impulse(4000), p);
    H = sum(h .* exp(-1i * w * (0:3999)'));
    pd(r, q) = -angle(H) / w - (10 + q / 10);
  end
end
s = dc_worst <= 1e-9;
m = sprintf('DC max|d| = %.3g; phase-delay err @100 Hz max|.|: lin %.2e ap %.2e lag %.2e (reported, FREEZE_AT_G2)', ...
            dc_worst, max(abs(pd(1, :))), max(abs(pd(2, :))), max(abs(pd(3, :))));
end

function [s, m] = t5_block_size(~)
x = seeded_noise(5000, 0.2); outs = {};
for B = [1 48 64]
  for prec = {'double', 'single'}
    p = base_params(2, true, prec{1}); p.Reader = 'lagrange3'; p.BlockSize = B;
    p.E(1).DelaySamples = 1230.37;
    st = mdd_init_state(p); y = zeros(size(x), prec{1});
    for i0 = 1:B:numel(x)
      i1 = min(i0 + B - 1, numel(x));
      [y(i0:i1), st] = mdd_process_block(x(i0:i1), p, st);
    end
    outs{end + 1} = y; %#ok<AGROW>
  end
end
s = isequal(outs{1}, outs{3}, outs{5}) && isequal(outs{2}, outs{4}, outs{6});
m = sprintf('B in {1,48,64}, double and single: bit-identical %s', tf(s));
end

function [s, m] = t6_reset(~)
p = base_params(3, true, 'single'); x = seeded_noise(4000, 0.2);
y1 = run_kernel(x, p); y2 = run_kernel(x, p);
s = isequal(y1, y2); m = sprintf('two runs after reset bit-identical %s', tf(s));
end

function [s, m] = t7_sanitation(~)
p = base_params(2, true); maxD = round(p.MaxDelayMs * p.Fs / 1000);
p.E(1).Feedback = NaN; p.E(2).Feedback = 5; p.Dry = -1; p.Wet = NaN;
p.E(1).InputSend = NaN; p.E(2).InputSend = 3; p.E(1).LowCutHz = -5; p.E(2).HighCutHz = 1e9;
p.E(1).DelaySamples = NaN; p.E(2).DelaySamples = 1e9;
sp = mdd_sanitize_params(p);
clamps_ok = sp.E(1).Feedback == 0 && sp.E(2).Feedback == 0.999 && sp.Dry == 0 && sp.Wet == 0 ...
  && sp.E(1).InputSend == 1 && sp.E(2).InputSend == 1 && sp.E(1).LowCutHz == 0 ...
  && sp.E(2).HighCutHz == 0.49 * p.Fs && sp.E(1).DelaySamples == 1 && sp.E(2).DelaySamples == maxD;
q = base_params(2, true); q.Wet = 1;
x = seeded_noise(3000, 0.2); x(100) = NaN; x(200) = Inf; x(300) = -Inf; x(400) = 1e-40;
[y, st] = run_kernel(x, q);
count_ok = st.diag.non_finite_samples == 3;
s = clamps_ok && all(isfinite(y)) && count_ok;
m = sprintf('clamps %s, outputs finite %s, non_finite_samples = %d (want 3)', ...
            tf(clamps_ok), tf(all(isfinite(y))), st.diag.non_finite_samples);
end

function [s, m] = t8_routing(~)
p = base_params(1, false);
[~, ~, taps] = run_kernel(impulse(5000), p);
single_ok = all(taps(:, 2) == 0);
q = base_params(3, false); q.E(2).DelaySamples = q.E(1).DelaySamples;
q.E(1).Feedback = 0; q.E(2).Feedback = 0; q.Dry = 0;
y = run_kernel(impulse(5000), q);
want = q.Wet * q.ParallelNorm * 2;
par_ok = max(abs(y)) == want;
s = single_ok && par_ok;
m = sprintf('SINGLE E2 tap all zero %s; PARALLEL D1=D2 peak %.6g (want %.6g)', tf(single_ok), max(abs(y)), want);
end

function [s, m] = t9_cpp_parity(~)
root = fileparts(fileparts(fileparts(fileparts(mfilename('fullpath')))));
fx = fullfile(root, 'matlab', 'multidelay_digi', 'fixtures_out', 'parity');
out = fullfile(root, 'tools', 'matlab_parity', 'out');
files = {fullfile(fx, 'P1_in.f32'), fullfile(out, 'P1_out.f32'), fullfile(out, 'P2_out.f32')};
if ~all(cellfun(@(f) exist(f, 'file') == 2, files))
  s = -1; m = 'parity files absent: run WP4 fixtures + WP5 dump (record T9 NOT_RUN)'; return;
end
x = read_f32(files{1}); d = zeros(1, 2);
for k = 1:2
  v = load(fullfile(fx, sprintf('P%d_params.txt', k)));
  p = mdd_default_params(); p.Precision = 'single'; p.Reader = 'integer'; p.Config = 1;
  p.TimeSmoothMs = 0; p.E(1).DelaySamples = v(1); p.E(1).Feedback = v(2);
  p.E(1).InputSend = v(3); p.Dry = v(4); p.Wet = v(5);
  p.E(1).LowCutHz = v(6); p.E(1).HighCutHz = v(7); p.BlockSize = v(8);
  y = run_kernel(single(x), p);
  d(k) = max(abs(double(y) - double(read_f32(files{k + 1}))));
end
s = d(1) == 0 && d(2) <= 1e-6;
m = sprintf('P1 filters off max|d| = %.3g (want 0); P2 filters on max|d| = %.3g (<= 1e-6)', d(1), d(2));
end

function v = read_f32(f)
fid = fopen(f, 'r', 'ieee-le'); v = fread(fid, Inf, 'float32=>single'); fclose(fid);
end

function [s, m] = t10_contract_markers(~)
% Contract v1 section 10 (VERIFIED in Octave 8.4.0 when the contract was written).
want_idx = [19200 38400 57600 76800 96000 115200 134400;
            19200 33600 38400 48000 52800 57600 62400;
            14400 19200 28800 38400 43200 57600 72000];
want_amp = [1 0.5 0.25 0.125 0.0625 0.03125 0.015625;
            1 1 0.5 0.5 0.5 0.25 0.25;
            0.5 0.5 0.25 0.25 0.125 0.1875 0.03125];
ok = true; worst = [0 0]; precs = {'double', 'single'};
for pr = 1:2
  for cfg = 1:3
    p = base_params(cfg, false, precs{pr}); p.Dry = 0; p.Wet = 1;
    p.E(1).DelaySamples = 19200; p.E(2).DelaySamples = 14400;
    p.E(1).Feedback = 0.5; p.E(2).Feedback = 0.5;
    y = double(run_kernel(impulse(140000), p));
    [idx, amp] = first_nonzero(y, 7);
    ok = ok && isequal(idx(:)', want_idx(cfg, :));
    if numel(amp) == 7, worst(pr) = max(worst(pr), max(abs(amp(:)' - want_amp(cfg, :)))); else, ok = false; end
  end
end
s = ok && worst(1) <= 1e-12 && worst(2) <= 1e-6;
m = sprintf('indices %s; amp max|d| double %.3g (<=1e-12), single %.3g (<=1e-6)', tf(ok), worst(1), worst(2));
end

function [s, m] = t11_controls(~)
p = mdd_default_params(); c = p.Ctl; ok = true; notes = {};
c.Time = 0; q = mdd_controls_to_params(c, p); ok = ok && q.E(1).DelaySamples == round(0.020 * p.Fs);
c.Time = 1; q = mdd_controls_to_params(c, p); ok = ok && q.E(1).DelaySamples == round(2.5 * p.Fs);
if ~ok, notes{end + 1} = 'time endpoints'; end
ratios = [1/4 1/3 3/8 1/2 2/3 3/4 1];
for i = 1:7
  c.RatioIndex = i; q = mdd_controls_to_params(c, p);
  if q.E(2).DelaySamples ~= round(ratios(i) * q.E(1).DelaySamples) || q.E(2).DelaySamples > q.E(1).DelaySamples
    ok = false; notes{end + 1} = sprintf('ratio %d', i); %#ok<AGROW>
  end
end
c = p.Ctl; want = [0.2929 0.5528 0.7764]; ks = [0.5 0.8 0.95];
for i = 1:3
  c.Feedback = ks(i) / 0.95;
  c.Config = 2; q = mdd_controls_to_params(c, p);
  if abs(q.E(1).Feedback - want(i)) > 1e-4 || q.E(1).Feedback ~= q.E(2).Feedback, ok = false; notes{end + 1} = 'series comp'; end %#ok<AGROW>
  for cfg = [1 3]
    c.Config = cfg; q = mdd_controls_to_params(c, p);
    if abs(q.E(1).Feedback - ks(i)) > 1e-12, ok = false; notes{end + 1} = 'f = k'; end %#ok<AGROW>
  end
end
c = p.Ctl; c.Feedback = 1; c.FeedbackE2 = 0.5; c.Config = 1; q = mdd_controls_to_params(c, p);
if abs(q.E(2).Feedback - 0.475) > 1e-12 || abs(q.E(1).Feedback - 0.95) > 1e-12, ok = false; notes{end + 1} = 'unlink'; end
c = p.Ctl; c.Color = 0; q = mdd_controls_to_params(c, p); ok1 = q.E(1).HighCutHz == 0.49 * p.Fs;
c.Color = 1; q = mdd_controls_to_params(c, p); ok2 = abs(q.E(1).HighCutHz - 2000) <= 1e-9;
ok3 = q.E(1).LowCutHz == 40 && q.E(2).LowCutHz == 40 && q.Dry == 1 && q.Wet == c.Mix;
if ~(ok1 && ok2 && ok3), ok = false; notes{end + 1} = 'color/mix'; end
s = ok;
if ok, m = 'TIME, ratio, FEEDBACK (+series comp, unlink), COLOR, MIX mapping as contract 5-6';
else, m = ['mismatch: ' strjoin(notes, ', ')]; end
end

function [s, m] = t12_series_buildup(~)
fs = 48000; n = (0:6 * fs - 1)'; x = 0.1 * sin(2 * pi * 100 * n / fs);
last = (5 * fs + 1):(6 * fs);
peak = zeros(1, 3); ypk = zeros(1, 3);
cases = {1, 0.95; 2, 1 - sqrt(1 - 0.95); 2, 0.95};   % SINGLE, SERIES compensated, SERIES uncompensated
for i = 1:3
  p = base_params(cases{i, 1}, false); p.Dry = 0; p.Wet = 1;
  p.E(1).DelaySamples = 480; p.E(2).DelaySamples = 480;
  p.E(1).Feedback = cases{i, 2}; p.E(2).Feedback = cases{i, 2};
  [y, ~, taps] = run_kernel(x, p);
  peak(i) = max(abs(taps(last, 1 + (cases{i, 1} == 2))));
  ypk(i) = max(abs(y(last)));
end
s = abs(peak(1) - 2) <= 0.01 && abs(peak(2) - 2) <= 0.01 && abs(peak(3) - 40) <= 0.1;
m = sprintf('last-engine tap peak: SINGLE %.4f, SERIES comp %.4f, SERIES uncomp %.4f; mixed y peak %.4f / %.4f / %.4f', ...
            peak, ypk);
end

function [s, m] = t13_soak(mode)
fs = 48000;
if strcmpi(mode, 'full'), tn = 50; ts = 10; win = fs; else, tn = 5; ts = 2; win = fs / 2; end
x = [seeded_noise(tn * fs, 0.1); zeros(ts * fs, 1)];
tail = (tn * fs + 1):numel(x);
ok = true; worst_peak = 0; clipped = 0; runs = 0;
for cfg = 1:3
  for k = [0 0.5 0.95]
    for color = [0 1]
      p = mdd_default_params(); c = p.Ctl;
      c.Config = cfg; c.Feedback = k / 0.95; c.Color = color; c.Mix = 1;
      p = mdd_controls_to_params(c, p);
      [y, st] = run_kernel(x, p);
      yt = y(tail); r0 = rms_of(yt(1:win)); r1 = rms_of(yt(end - win + 1:end));
      decays = r1 < r0 || (r0 == 0 && r1 == 0);
      ok = ok && all(isfinite(y)) && decays;
      worst_peak = max(worst_peak, st.diag.max_abs_sample);
      clipped = clipped + st.diag.clipped_samples; runs = runs + 1;
    end
  end
end
s = ok;
m = sprintf('%s: %d runs finite+decaying %s; worst peak %.3f, clipped samples total %d (recorded, FREEZE_AT_G2)', ...
            mode, runs, tf(ok), worst_peak, clipped);
end

function [s, m] = t14_system_object(~)
if exist('OCTAVE_VERSION', 'builtin')
  s = -1; m = 'matlab.System wrapper: MATLAB only'; return;
end
p = base_params(2, true, 'single'); x = seeded_noise(480, 0.2);
obj = MultiDelayRef('Params', p);
y = zeros(size(x), 'single');
for b = 1:10
  i = (b - 1) * 48 + (1:48);
  y(i) = obj(single(x(i)));
end
yr = run_kernel(single(x), p);
d = obj.diagnostics();
s = isequal(y, yr) && d.processed_blocks == 10;
m = sprintf('step x10 vs kernel bit-identical %s; processed_blocks %d', tf(isequal(y, yr)), d.processed_blocks);
end

function t = tf(b)
if b, t = 'yes'; else, t = 'NO'; end
end
