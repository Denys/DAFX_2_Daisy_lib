function multidelay_fixtures(outDir, varargin)
% MULTIDELAY_FIXTURES  Stimulus + raw output + JSON metrics for M1-M3, F1-F7 and the
%   C++ parity inputs (P1/P2). No pass/fail thresholds: every JSON carries
%   "verdict": "NOT_JUDGED_THRESHOLDS_FREEZE_AT_G2".
%   multidelay_fixtures('fixtures_out', 'Quick', true)   durations x 0.1
%   Raw files: <ID>_in.f32 / <ID>_out.f32 (little-endian float32) of the representative
%   case named in the JSON field "raw_case"; metrics cover every case.
quick = false;
for i = 1:2:numel(varargin)
  if strcmpi(varargin{i}, 'Quick'), quick = logical(varargin{i + 1}); end
end
here = fileparts(mfilename('fullpath'));
addpath(here);
if ~exist(outDir, 'dir'), mkdir(outDir); end
scale = 1; if quick, scale = 0.1; end
if exist('OCTAVE_VERSION', 'builtin')
  rand('seed', 1); randn('seed', 1); seeding = 'octave rand/randn(''seed'',1)'; %#ok<RAND>
else
  rng(1); seeding = 'matlab rng(1)';
end
fs = 48000; ctx = struct('dir', outDir, 'seeding', seeding, 'quick', quick);

parity(outDir, fs);
m_markers(ctx);
f1_alignment(ctx);
f2_fractional(ctx, fs);
f3_sweep(ctx, fs, scale);
f4_modulation(ctx, fs, scale);
f5_stability(ctx, fs, scale);
f6_wrap(ctx);
f7_malformed(ctx);
fprintf('fixtures written to %s (quick=%d, %s)\n', outDir, quick, seeding);
end

% ------------------------------------------------------------------ fixtures
function parity(outDir, fs)
d = fullfile(outDir, 'parity'); if ~exist(d, 'dir'), mkdir(d); end
x = [1; zeros(19999, 1); 0.1 * randn(fs, 1)];            % impulse + 1 s noise at -20 dBFS
write_f32(fullfile(d, 'P1_in.f32'), x);
write_text(fullfile(d, 'P1_params.txt'), '1230 0.6 1 0 1 0 23520 48');
write_text(fullfile(d, 'P2_params.txt'), '1230 0.6 1 0 1 80 4000 48');
end

function m_markers(ctx)
names = {'SINGLE', 'DUAL_SERIES', 'DUAL_PARALLEL'};
for cfg = 1:3
  p = engine_params(cfg, 'integer', 'double');
  p.E(1).DelaySamples = 19200; p.E(2).DelaySamples = 14400;
  p.E(1).Feedback = 0.5; p.E(2).Feedback = 0.5;
  x = impulse(140000); y = run(x, p);
  [b, a] = mdd_transfer_oracle(p);
  idx = find(abs(y) > 1e-15); idx = idx(1:min(7, numel(idx)));
  met = struct('markers_index', (idx - 1)', 'markers_amplitude', y(idx)', ...
               'oracle_max_abs_diff', max(abs(y - filter(b, a, x))));
  save_fixture(ctx, sprintf('M%d', cfg), x, y, ...
    sprintf('unit impulse, %s, contract v1 section 10 settings', names{cfg}), p, met, 'only case');
end
end

function f1_alignment(ctx)
p0 = engine_params(1, 'integer', 'single'); maxD = round(p0.MaxDelayMs * p0.Fs / 1000);
Ds = [1 2 maxD]; err = zeros(1, 3); nf = zeros(1, 3);
for i = 1:3
  p = p0; p.E(1).DelaySamples = Ds(i);
  x = impulse(Ds(i) + 100); [y, st] = run(x, p);
  [~, k] = max(abs(y)); err(i) = (k - 1) - Ds(i); nf(i) = st.diag.non_finite_samples;
end
save_fixture(ctx, 'F1', x, y, 'impulses at D = 1, 2, max (mono)', p, ...
  struct('delay_samples', Ds, 'alignment_error_samples', err, 'non_finite_samples', nf), 'D = max');
end

function f2_fractional(ctx, fs)
readers = {'linear', 'allpass1', 'lagrange3'}; N = 4096;
f = (0:N / 2)' * fs / N; band = f <= 20000; w = 2 * pi * f / fs;
mag_db = zeros(3, 9); ph = zeros(3, 9);
for r = 1:3
  for q = 1:9
    D = 10 + q / 10; p = engine_params(1, readers{r}, 'double'); p.E(1).DelaySamples = D;
    x = impulse(N); y = run(x, p);
    H = fft(y); H = H(1:N / 2 + 1);
    ideal = exp(-1i * w * D);
    mag_db(r, q) = max(abs(20 * log10(abs(H(band)))));
    ph(r, q) = max(abs(angle(H(band) ./ ideal(band))));
  end
end
save_fixture(ctx, 'F2', x, y, 'impulses at D = 10 + 0.1..0.9 per fractional reader, up to 20 kHz', p, ...
  struct('readers', {readers}, 'fraction', (1:9) / 10, 'max_abs_magnitude_error_db', mag_db, ...
         'max_abs_phase_error_rad', ph), 'lagrange3, D = 10.9');
end

function f3_sweep(ctx, fs, scale)
readers = {'integer', 'linear', 'allpass1', 'lagrange3'}; T = 10 * scale;
n = (0:round(T * fs) - 1)'; f0 = 20; f1 = 20000;
x = 0.5 * sin(2 * pi * f0 * T / log(f1 / f0) * (exp(n / fs / T * log(f1 / f0)) - 1));
X = fft(x); N = numel(x); f = (0:N - 1)' * fs / N; band = f >= 20 & f <= 20000;
ripple = zeros(1, 4); gd_err = zeros(1, 4); D = 10.5;
for r = 1:4
  p = engine_params(1, readers{r}, 'double'); p.E(1).DelaySamples = D;
  y = run(x, p); H = fft(y) ./ X; Hb = H(band); wb = 2 * pi * f(band) / fs;
  ripple(r) = max(20 * log10(abs(Hb))) - min(20 * log10(abs(Hb)));
  gd = -diff(unwrap(angle(Hb))) ./ diff(wb);
  dref = D; if r == 1, dref = floor(D + 0.5); end
  gd_err(r) = max(abs(gd - dref));
end
save_fixture(ctx, 'F3', x, y, sprintf('log sweep 20 Hz-20 kHz, %.1f s, static D = 10.5', T), p, ...
  struct('readers', {readers}, 'passband_ripple_db', ripple, 'group_delay_error_samples', gd_err), 'lagrange3');
end

function f4_modulation(ctx, fs, scale)
T = 20 * scale; N = round(T * fs); n = (0:N - 1)';
stims = {0.5 * sin(2 * pi * 440 * n / fs), ...
         0.5 * sin(2 * pi * (100 + 900 * n / N) .* n / fs), ...
         0.1 * randn(N, 1)};
snames = {'tone_440', 'chirp_100_1000', 'noise_-20dBFS'}; ramps = {'slow', 'fast'};
res = struct('stimulus', {}, 'ramp', {}, 'max_first_difference', {}, ...
             'energy_above_20k_db', {}, 'final_trajectory_error_samples', {});
for s = 1:3
  for r = 1:2
    p = engine_params(1, 'linear', 'double'); p.TimeSmoothMs = 20; B = p.BlockSize;
    st = mdd_init_state(p); y = zeros(N, 1); x = stims{s};
    for i0 = 1:B:N
      i1 = min(i0 + B - 1, N); t = (i0 - 1) / N;
      if r == 1, D = 480 + 480 * t; else, D = 480 + 480 * mod(floor(t * 20), 2); end
      p.E(1).DelaySamples = D;
      [y(i0:i1), st] = mdd_process_block(x(i0:i1), p, st);
    end
    Y = abs(fft(y)) .^ 2; fr = (0:N - 1)' * fs / N; hi = fr > 20000 & fr < fs - 20000;
    res(end + 1) = struct('stimulus', snames{s}, 'ramp', ramps{r}, ...
      'max_first_difference', max(abs(diff(y))), ...
      'energy_above_20k_db', 10 * log10(sum(Y(hi)) / sum(Y) + eps), ...
      'final_trajectory_error_samples', abs(st.Dcur(1) - D)); %#ok<AGROW>
  end
end
save_fixture(ctx, 'F4', x, y, sprintf('tone/chirp/noise %.1f s, TIME ramps 480->960 samples (slow line, fast 20-step square), smoothing 20 ms, linear reader', T), ...
  p, struct('cases', res), 'noise, fast ramp');
end

function f5_stability(ctx, fs, scale)
T = 300 * scale; N = round(T * fs); n = (0:N - 1)';
stims = {zeros(N, 1), impulse(N), 0.5 * ones(N, 1), 0.1 * randn(N, 1), sin(2 * pi * 100 * n / fs)};
snames = {'silence', 'impulse', 'dc_0.5', 'noise_-20dBFS', 'sine_0dBFS_100Hz'};
res = struct('stimulus', {}, 'feedback', {}, 'rms_envelope_1s', {}, 'peak', {}, 'self_growth', {});
for s = 1:5
  for fbv = [0 0.5 0.999]
    p = engine_params(1, 'integer', 'single'); p.E(1).DelaySamples = 480; p.E(1).Feedback = fbv;
    [y, st] = run(stims{s}, p); y = double(y);
    env = sqrt(mean(reshape(y(1:floor(N / fs) * fs), fs, []) .^ 2, 1));
    res(end + 1) = struct('stimulus', snames{s}, 'feedback', fbv, 'rms_envelope_1s', env, ...
      'peak', st.diag.max_abs_sample, 'self_growth', env(end) > env(max(1, end - 1)) * 1.0001 && env(end) > 0); %#ok<AGROW>
  end
end
save_fixture(ctx, 'F5', stims{s}, y, sprintf('stability soak %.0f s, SINGLE, D = 480, HP 40 Hz (contract), LP off', T), ...
  p, struct('cases', res), 'sine 0 dBFS, fb 0.999');
end

function f6_wrap(ctx)
p = engine_params(1, 'integer', 'single');
maxD = round(p.MaxDelayMs * p.Fs / 1000); p.E(1).DelaySamples = maxD; p.E(1).Feedback = 0;
st = mdd_init_state(p); N = 2 * st.cap + 1000;
x = single(0.1 * randn(N, 1)); y = run(x, p);
ref = [zeros(maxD, 1, 'single'); x(1:end - maxD)];
save_fixture(ctx, 'F6', x, y, 'noise at max delay across two ring wraps, mono', p, ...
  struct('max_abs_error_vs_ideal_delay', double(max(abs(y - ref))), 'ring_capacity', st.cap, ...
         'stereo_part', 'N/A (mono-first, ADR-0019)'), 'only case');
end

function f7_malformed(ctx)
p = engine_params(2, 'integer', 'single');
p.E(1).Feedback = NaN; p.E(2).Feedback = Inf; p.Dry = -Inf; p.Wet = 7;
p.E(1).LowCutHz = NaN; p.E(2).HighCutHz = -1; p.E(1).DelaySamples = -5; p.E(2).DelaySamples = Inf;
x = 0.1 * randn(20000, 1); x(10:10:100) = NaN; x(1000) = Inf; x(2000) = -Inf; x(3000:3010) = 1e-40;
[y, st] = run(x, p); sp = mdd_sanitize_params(p);
save_fixture(ctx, 'F7', x, y, 'malformed parameters, NaN/Inf input, denormals', p, ...
  struct('injected_non_finite', 12, 'non_finite_samples', st.diag.non_finite_samples, ...
         'all_output_finite', all(isfinite(y)), 'max_abs_output', st.diag.max_abs_sample, ...
         'sanitized_feedback', [sp.E.Feedback], 'sanitized_delay', [sp.E.DelaySamples], ...
         'sanitized_dry_wet', [sp.Dry sp.Wet]), 'only case');
end

% ------------------------------------------------------------------ helpers
function p = engine_params(cfg, reader, precision)
p = mdd_default_params(); p.Config = cfg; p.Reader = reader; p.Precision = precision;
p.Dry = 0; p.Wet = 1;
for e = 1:2
  p.E(e).Feedback = 0; p.E(e).InputSend = 1; p.E(e).LowCutHz = 0; p.E(e).HighCutHz = 0.49 * p.Fs;
end
end

function [y, st] = run(x, p)
st = mdd_init_state(p); [y, st] = mdd_process_block(x, p, st);
end

function x = impulse(N)
x = zeros(N, 1); x(1) = 1;
end

function save_fixture(ctx, id, x, y, stimulus, p, metrics, raw_case)
fi = fullfile(ctx.dir, [id '_in.f32']); fo = fullfile(ctx.dir, [id '_out.f32']);
write_f32(fi, x); write_f32(fo, y);
p = rmfield(p, 'Ctl');
s = struct('id', id, 'stimulus', stimulus, 'raw_case', raw_case, 'quick', ctx.quick, ...
           'seeding', ctx.seeding, 'params', p, 'metrics', metrics, ...
           'sha256', struct('in_f32', sha256_file(fi), 'out_f32', sha256_file(fo)), ...
           'verdict', 'NOT_JUDGED_THRESHOLDS_FREEZE_AT_G2');
write_text(fullfile(ctx.dir, [id '.json']), jsonencode(s));
end

function write_f32(f, v)
fid = fopen(f, 'w', 'ieee-le'); fwrite(fid, single(v), 'float32'); fclose(fid);
end

function write_text(f, s)
fid = fopen(f, 'w'); fprintf(fid, '%s\n', s); fclose(fid);
end

function h = sha256_file(f)
fid = fopen(f, 'r'); bytes = fread(fid, Inf, 'uint8=>uint8'); fclose(fid);
if exist('OCTAVE_VERSION', 'builtin')
  h = hash('sha256', char(bytes'));
else
  md = java.security.MessageDigest.getInstance('SHA-256');
  d = typecast(md.digest(bytes), 'uint8');
  h = lower(reshape(dec2hex(d, 2)', 1, []));
end
end
