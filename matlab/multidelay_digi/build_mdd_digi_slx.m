function info = build_mdd_digi_slx(params, varargin)
% BUILD_MDD_DIGI_SLX  Build the Simulink block diagram of the mono-first DIGI graph
%   (ADR-0019, contract v1) programmatically, export SVGs and run a smoke simulation.
%   info = build_mdd_digi_slx(mdd_default_params(), 'ExportSvg', true, 'Simulate', true)
%   Core Simulink blocks only (no DSP System Toolbox). The .slx is saved next to this file
%   and is gitignored; the SVGs in docs/slx_export/ are the committed deliverable.
%
%   Colour legend: green = mirrors VERIFIED main code (engine law); orange = PROPOSED
%   topology accepted in contract v1 (router, mixer, gains); grey = HOLD.
opt = struct('ExportSvg', true, 'Simulate', true);
for i = 1:2:numel(varargin), opt.(varargin{i}) = varargin{i + 1}; end
here = fileparts(mfilename('fullpath')); addpath(here);
GREEN = '[0.80 0.93 0.80]'; ORANGE = '[1.00 0.87 0.70]'; GREY = '[0.88 0.88 0.88]';
info = struct('simulink', ver('simulink'), 'loop_variant', ...
  'B: Unit Delay + variable Delay (D-1); a single variable Delay forms an algebraic loop', ...
  'svg', {{}}, 'smoke', []);

mdl = 'mdd_digi_topology';
if bdIsLoaded(mdl), close_system(mdl, 0); end
slx = fullfile(here, [mdl '.slx']);
if exist(slx, 'file'), delete(slx); end         % previous build output (gitignored)
new_system(mdl); load_system('simulink');
set_param(mdl, 'SolverType', 'Fixed-step', 'Solver', 'FixedStepDiscrete', 'FixedStep', '1/Fs', ...
          'AlgebraicLoopMsg', 'error');

% ---------------------------------------------------------------- top level
blk('simulink/Sources/From Workspace', [mdl '/x_in'], 'VariableName', 'x_in', 'SampleTime', '1/Fs', ...
    'Interpolate', 'off', 'OutputAfterFinalValue', 'Setting to zero', 'Position', [40 190 120 220]);
blk('simulink/Sources/Constant', [mdl '/DIGI_CONFIG'], 'Value', 'DIGI_CONFIG', ...
    'BackgroundColor', GREY, 'Position', [40 470 130 500]);
e1 = [mdl '/E1 mono delay engine']; e2 = [mdl '/E2 mono delay engine'];
rt = [mdl '/DIGI CONFIG router']; mx = [mdl '/Output mixer'];
build_engine(e1, 'E1', [260 170 420 250], GREEN);
build_engine(e2, 'E2', [520 360 680 440], GREEN);
build_router(rt, [260 350 420 470], ORANGE, params.SeriesFeedPoint);
build_mixer(mx, [800 170 960 470], ORANGE);
blk('simulink/Sinks/Out1', [mdl '/y'], 'Position', [1040 310 1070 330]);
blk('simulink/Sinks/To Workspace', [mdl '/y_out'], 'VariableName', 'y_out', 'SaveFormat', 'Array', ...
    'Position', [1040 230 1120 260]);
blk('simulink/Sinks/Scope', [mdl '/Scope'], 'Position', [1040 380 1070 410]);
blk('simulink/Sinks/Terminator', [mdl '/unused E2 write'], 'Position', [715 404 735 422]);

ln(mdl, 'x_in/1', 'E1 mono delay engine/1');
ln(mdl, 'x_in/1', 'DIGI CONFIG router/1');
ln(mdl, 'x_in/1', 'Output mixer/1');
ln(mdl, 'E1 mono delay engine/1', 'Output mixer/2');
if strcmp(params.SeriesFeedPoint, 'write')
  ln(mdl, 'E1 mono delay engine/2', 'DIGI CONFIG router/2');
else
  ln(mdl, 'E1 mono delay engine/1', 'DIGI CONFIG router/2');
  blk('simulink/Sinks/Terminator', [mdl '/unused E1 write'], 'Position', [460 230 480 250]);
  ln(mdl, 'E1 mono delay engine/2', 'unused E1 write/1');
end
ln(mdl, 'DIGI_CONFIG/1', 'DIGI CONFIG router/3');
ln(mdl, 'DIGI_CONFIG/1', 'Output mixer/4');
ln(mdl, 'DIGI CONFIG router/1', 'E2 mono delay engine/1');
ln(mdl, 'E2 mono delay engine/1', 'Output mixer/3');
ln(mdl, 'E2 mono delay engine/2', 'unused E2 write/1');
ln(mdl, 'Output mixer/1', 'y/1');
ln(mdl, 'Output mixer/1', 'y_out/1');
ln(mdl, 'Output mixer/1', 'Scope/1');

note(mdl, [40 20 700 110], sprintf(['Mono-first DIGI (ADR-0019, contract v1 accepted 2026-09-25): SINGLE | DUAL SERIES | DUAL PARALLEL, one output.\n' ...
  'Legend:  GREEN = mirrors VERIFIED main code (DigitalDelayNode @73976da)   ORANGE = PROPOSED topology (contract v1)   GREY = HOLD\n' ...
  'DIGI_CONFIG: 1 SINGLE, 2 DUAL SERIES, 3 DUAL PARALLEL. Transition policy HOLD: a change is a hard switch at the block\n' ...
  'boundary (possible click); histories are never cleared.']), '[1 1 1]');
note(mdl, [40 520 520 560], 'DIGI_CONFIG transition = hard switch (HOLD: crossfade deferred to v2, fixture F8)', GREY);
note(mdl, [760 520 1180 570], sprintf('Future true-stereo seam: HOLD, not enabled.\nEngines stay mono; a stereo version gives each engine L/R histories.'), GREY);

% ---------------------------------------------------------------- parameters
set_model_params(mdl, params, impulse(20000));
info.data_port_order = struct( ...
  'router', get_param([rt '/DIGI CONFIG switch'], 'DataPortOrder'), ...
  'mixer', get_param([mx '/gain select'], 'DataPortOrder'), ...
  'default_case', get_param([rt '/DIGI CONFIG switch'], 'DiagnosticForDefault'));
info.delay_length_source = get_param([e1 '/History (D-1) + integer reader'], 'DelayLengthSource');
fprintf('DataPortOrder router = %s, mixer = %s; default case = %s; Delay length source = %s\n', ...
        info.data_port_order.router, info.data_port_order.mixer, info.data_port_order.default_case, ...
        info.delay_length_source);
save_system(mdl, fullfile(here, [mdl '.slx']));

% ---------------------------------------------------------------- export
if opt.ExportSvg
  d = fullfile(here, 'docs', 'slx_export'); if ~exist(d, 'dir'), mkdir(d); end
  views = {mdl, '00_top'; e1, '01_engine'; rt, '02_router'; mx, '03_mixer'};
  for k = 1:4
    f = fullfile(d, [views{k, 2} '.svg']);
    print(['-s' views{k, 1}], '-dsvg', f);
    info.svg{end + 1} = f;
  end
end

% ---------------------------------------------------------------- smoke simulation
if opt.Simulate
  names = {'SINGLE', 'DUAL_SERIES', 'DUAL_PARALLEL'}; r = struct('config', {}, 'filters', {}, 'max_abs_diff', {});
  for cfg = 1:3
    for filt = [false true]
      p = smoke_params(params, cfg, filt); x = impulse(20000);
      set_model_params(mdl, p, x);
      out = sim(mdl);
      st = mdd_init_state(p); yk = mdd_process_block(x, p, st);
      r(end + 1) = struct('config', names{cfg}, 'filters', filt, 'max_abs_diff', max(abs(out.y_out(:) - yk))); %#ok<AGROW>
      fprintf('smoke %-13s filters %d: max|d| = %.3g\n', names{cfg}, filt, r(end).max_abs_diff);
    end
  end
  info.smoke = r;
  set_model_params(mdl, params, impulse(20000));
  save_system(mdl);
end
end

% ================================================================= subsystems
function build_engine(sys, P, pos, colour)
new_subsystem(sys, pos, colour);
blk('simulink/Sources/In1', [sys '/in'], 'Position', [30 100 60 114]);
blk('simulink/Math Operations/Gain', [sys '/InputSend'], 'Gain', [P '_send'], 'Position', [95 90 185 124]);
blk('simulink/Math Operations/Sum', [sys '/write node'], 'Inputs', '++', 'Position', [230 90 260 124]);
blk('simulink/Discrete/Unit Delay', [sys '/History (1 frame)'], 'SampleTime', '1/Fs', 'Position', [320 90 370 124]);
blk('simulink/Discrete/Delay', [sys '/History (D-1) + integer reader'], 'DelayLengthSource', 'Input port', ...
    'DelayLengthUpperLimit', 'Nmax', 'InitialCondition', '0', 'SampleTime', '1/Fs', 'Position', [430 85 520 145]);
blk('simulink/Sources/Constant', [sys '/D - 1'], 'Value', [P '_Dm1'], 'Position', [340 160 390 180]);
blk('simulink/Sinks/Out1', [sys '/tap'], 'Position', [640 105 670 119]);
blk('simulink/Sinks/Out1', [sys '/write'], 'Position', [320 30 350 44]);
blk('simulink/Discrete/Discrete Filter', [sys '/HP LowCut'], 'Numerator', [P '_hp_num'], ...
    'Denominator', [P '_hp_den'], 'SampleTime', '1/Fs', 'Orientation', 'left', 'Position', [520 230 600 270]);
blk('simulink/Discrete/Discrete Filter', [sys '/LP HighCut'], 'Numerator', [P '_lp_num'], ...
    'Denominator', [P '_lp_den'], 'SampleTime', '1/Fs', 'Orientation', 'left', 'Position', [370 230 450 270]);
blk('simulink/Math Operations/Gain', [sys '/Feedback (clamp 0.999)'], 'Gain', [P '_fb'], ...
    'Orientation', 'left', 'Position', [220 230 280 270]);
ln(sys, 'in/1', 'InputSend/1');
ln(sys, 'InputSend/1', 'write node/1');
ln(sys, 'write node/1', 'History (1 frame)/1');
ln(sys, 'write node/1', 'write/1');
ln(sys, 'History (1 frame)/1', 'History (D-1) + integer reader/1');
ln(sys, 'D - 1/1', 'History (D-1) + integer reader/2');
ln(sys, 'History (D-1) + integer reader/1', 'tap/1');
ln(sys, 'History (D-1) + integer reader/1', 'HP LowCut/1');
ln(sys, 'HP LowCut/1', 'LP HighCut/1');
ln(sys, 'LP HighCut/1', 'Feedback (clamp 0.999)/1');
ln(sys, 'Feedback (clamp 0.999)/1', 'write node/2');
kids = find_system(sys, 'SearchDepth', 1, 'Type', 'Block');
for k = 2:numel(kids), set_param(kids{k}, 'BackgroundColor', colour); end
note(sys, [30 300 700 380], sprintf(['Law mirrors DigitalDelayNode::Process @73976da, digital_delay_node.hpp:73-143 (filters 180-227):\n' ...
  'tap = h[D];  cond = LP(HP(tap));  h <- send*in + fb*cond.   HP: p = exp(-2*pi*fLo/fs); LP: a = 1 - exp(-2*pi*fHi/fs); disabled stage = 1.\n' ...
  'Reader here: integer only. Fractional readers (layer 2, PROPOSED) exist in the MATLAB kernel only.\n' ...
  'History = Unit Delay + variable Delay (D-1): a single variable Delay closes an algebraic loop in Simulink.']), '[1 1 1]');
end

function build_router(sys, pos, colour, feed)
new_subsystem(sys, pos, colour);
blk('simulink/Sources/In1', [sys '/x'], 'Position', [30 160 60 174]);
blk('simulink/Sources/In1', [sys '/E1 feed'], 'Position', [30 110 60 124]);
blk('simulink/Sources/In1', [sys '/cfg'], 'Position', [30 20 60 34]);
blk('simulink/Sources/Constant', [sys '/E2 idle input (SINGLE)'], 'Value', '0', 'Position', [100 55 150 80]);
blk('simulink/Signal Routing/Multiport Switch', [sys '/DIGI CONFIG switch'], 'Inputs', '3', ...
    'DataPortOrder', 'One-based contiguous', 'DiagnosticForDefault', 'Error', 'Position', [240 20 290 190]);
blk('simulink/Sinks/Out1', [sys '/e2_in'], 'Position', [360 100 390 114]);
ln(sys, 'cfg/1', 'DIGI CONFIG switch/1');
ln(sys, 'E2 idle input (SINGLE)/1', 'DIGI CONFIG switch/2');
ln(sys, 'E1 feed/1', 'DIGI CONFIG switch/3');
ln(sys, 'x/1', 'DIGI CONFIG switch/4');
ln(sys, 'DIGI CONFIG switch/1', 'e2_in/1');
set_param([sys '/DIGI CONFIG switch'], 'BackgroundColor', colour);
note(sys, [30 220 560 290], sprintf(['ADR-0019. SINGLE: E2 input 0 (E2 keeps running and decays).\n' ...
  'SERIES: E2 fed by E1 (feed point = %s: E2 repeats E1''s echoes). PARALLEL: E2 fed by x.\n' ...
  'Data ports one-based: 1 -> SINGLE, 2 -> SERIES, 3 -> PARALLEL; an out-of-range DIGI_CONFIG is an error. Ping-Pong REJECTED.'], feed), '[1 1 1]');
end

function build_mixer(sys, pos, colour)
new_subsystem(sys, pos, colour);
blk('simulink/Sources/In1', [sys '/x'], 'Position', [30 330 60 344]);
blk('simulink/Sources/In1', [sys '/tap1'], 'Position', [30 200 60 214]);
blk('simulink/Sources/In1', [sys '/tap2'], 'Position', [30 260 60 274]);
blk('simulink/Sources/In1', [sys '/cfg'], 'Position', [30 20 60 34]);
blk('simulink/Sources/Constant', [sys '/g SINGLE [1 0]'], 'Value', 'G_SINGLE', 'Position', [90 45 190 70]);
blk('simulink/Sources/Constant', [sys '/g SERIES [1 1]'], 'Value', 'G_SERIES', 'Position', [90 105 190 130]);
blk('simulink/Sources/Constant', [sys '/g PARALLEL 0.5*[1 1]'], 'Value', 'G_PARALLEL', 'Position', [90 165 190 190]);
blk('simulink/Signal Routing/Multiport Switch', [sys '/gain select'], 'Inputs', '3', ...
    'DataPortOrder', 'One-based contiguous', 'DiagnosticForDefault', 'Error', 'Position', [240 20 280 220]);
blk('simulink/Signal Routing/Demux', [sys '/g1 g2'], 'Outputs', '2', 'Position', [320 100 325 140]);
blk('simulink/Math Operations/Product', [sys '/g1*tap1'], 'Position', [380 190 420 225]);
blk('simulink/Math Operations/Product', [sys '/g2*tap2'], 'Position', [380 250 420 285]);
blk('simulink/Math Operations/Sum', [sys '/wet sum'], 'Inputs', '++', 'Position', [470 215 500 260]);
blk('simulink/Math Operations/Gain', [sys '/Wet'], 'Gain', 'Wet', 'Position', [540 222 590 252]);
blk('simulink/Math Operations/Gain', [sys '/Dry'], 'Gain', 'Dry', 'Position', [540 322 590 352]);
blk('simulink/Math Operations/Sum', [sys '/output sum'], 'Inputs', '++', 'Position', [640 265 670 310]);
blk('simulink/Sinks/Out1', [sys '/y'], 'Position', [720 280 750 294]);
ln(sys, 'cfg/1', 'gain select/1');
ln(sys, 'g SINGLE [1 0]/1', 'gain select/2');
ln(sys, 'g SERIES [1 1]/1', 'gain select/3');
ln(sys, 'g PARALLEL 0.5*[1 1]/1', 'gain select/4');
ln(sys, 'gain select/1', 'g1 g2/1');
ln(sys, 'g1 g2/1', 'g1*tap1/1'); ln(sys, 'tap1/1', 'g1*tap1/2');
ln(sys, 'g1 g2/2', 'g2*tap2/1'); ln(sys, 'tap2/1', 'g2*tap2/2');
ln(sys, 'g1*tap1/1', 'wet sum/1'); ln(sys, 'g2*tap2/1', 'wet sum/2');
ln(sys, 'wet sum/1', 'Wet/1'); ln(sys, 'x/1', 'Dry/1');
ln(sys, 'Wet/1', 'output sum/1'); ln(sys, 'Dry/1', 'output sum/2');
ln(sys, 'output sum/1', 'y/1');
kids = find_system(sys, 'SearchDepth', 1, 'Type', 'Block');
for k = 2:numel(kids)
  if ~any(strcmp(get_param(kids{k}, 'BlockType'), {'Inport', 'Outport'}))
    set_param(kids{k}, 'BackgroundColor', colour);
  end
end
note(sys, [30 380 760 450], sprintf(['y = Dry*x + Wet*(g1*tap1 + g2*tap2), contract v1 section 4 (accepted 2026-09-25):\n' ...
  'SINGLE g = [1 0];  SERIES g = [1 1] (both audible);  PARALLEL g = 0.5*[1 1] (peak-safe when echoes coincide).\n' ...
  'SERIES feedback per engine f = 1 - sqrt(1 - k) (contract section 6), applied in E1_fb / E2_fb.']), '[1 1 1]');
end

% ================================================================= helpers
function new_subsystem(sys, pos, colour)
blk('simulink/Ports & Subsystems/Subsystem', sys, 'Position', pos, 'BackgroundColor', colour);
delete_line(sys, 'In1/1', 'Out1/1');
delete_block([sys '/In1']); delete_block([sys '/Out1']);
end

function blk(lib, dst, varargin)
try
  add_block(lib, dst, varargin{:});
catch err
  error('mdd:block', 'add_block failed for library path "%s" -> "%s": %s', lib, dst, err.message);
end
end

function ln(sys, a, b)
add_line(sys, a, b, 'autorouting', 'smart');
end

function note(sys, pos, text, bg)
a = Simulink.Annotation(sys, text);
a.Position = pos; a.BackgroundColor = bg;
end

function set_model_params(mdl, p, x)
sp = mdd_sanitize_params(p); ws = get_param(mdl, 'ModelWorkspace');
N = numel(x); fs = sp.Fs;
assignin(ws, 'Fs', fs);
assignin(ws, 'Nmax', sp.MaxDelaySamples);
assignin(ws, 'DIGI_CONFIG', sp.Config);
assignin(ws, 'Dry', sp.Dry); assignin(ws, 'Wet', sp.Wet);
assignin(ws, 'G_SINGLE', [1 0]); assignin(ws, 'G_SERIES', sp.SeriesGains);
assignin(ws, 'G_PARALLEL', sp.ParallelNorm * [1 1]);
guard = 0.49 * fs;
for e = 1:2
  E = sp.E(e); P = sprintf('E%d', e);
  hpn = 1; hpd = 1; lpn = 1; lpd = 1;
  if E.LowCutHz > 0, q = exp(-2 * pi * E.LowCutHz / fs); hpn = [q -q]; hpd = [1 -q]; end
  if E.HighCutHz < guard, al = 1 - exp(-2 * pi * E.HighCutHz / fs); lpn = al; lpd = [1 -(1 - al)]; end
  assignin(ws, [P '_send'], E.InputSend); assignin(ws, [P '_fb'], E.Feedback);
  assignin(ws, [P '_Dm1'], floor(E.DelaySamples + 0.5) - 1);
  assignin(ws, [P '_hp_num'], hpn); assignin(ws, [P '_hp_den'], hpd);
  assignin(ws, [P '_lp_num'], lpn); assignin(ws, [P '_lp_den'], lpd);
end
assignin(ws, 'x_in', [(0:N - 1)' / fs, x(:)]);
set_param(mdl, 'StopTime', sprintf('%.17g', (N - 1) / fs));   % seconds, not samples
end

function p = smoke_params(p, cfg, filt)
p.Precision = 'double'; p.Reader = 'integer'; p.Config = cfg; p.Dry = 1; p.Wet = 0.7; p.TimeSmoothMs = 0;
p.E(1).DelaySamples = 1230; p.E(2).DelaySamples = 770; p.E(1).Feedback = 0.6; p.E(2).Feedback = 0.45;
for e = 1:2
  p.E(e).InputSend = 1;
  if filt, p.E(e).LowCutHz = 80; p.E(e).HighCutHz = 4000; else, p.E(e).LowCutHz = 0; p.E(e).HighCutHz = 0.49 * p.Fs; end
end
end

function x = impulse(N)
x = zeros(N, 1); x(1) = 1;
end
