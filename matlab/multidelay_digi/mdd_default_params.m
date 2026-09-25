function p = mdd_default_params()
% MDD_DEFAULT_PARAMS  Model parameters with the contract v1 defaults.
%   Contract: custom-pedals work_products/delay/runs/2026-09-24_mono_first_digi_contract_v1.md
%   (CONTRACT_ACCEPTED 2026-09-25). Engine-level fields (E(k), Dry, Wet, Config) are
%   derived from the front-panel controls in p.Ctl by mdd_controls_to_params.
p.Fs = 48000;
p.MaxDelayMs = 2500;           % contract 5: TIME up to 2.5 s per engine
p.BlockSize = 48;
p.Reader = 'integer';          % 'integer' (parity with main) | 'linear' | 'allpass1' | 'lagrange3'
p.Precision = 'single';        % 'single' (target float32) | 'double'
p.TimeSmoothMs = 0;            % 0 = jump, as on main
p.SeriesFeedPoint = 'tap';     % contract 4: E2 hears E1's echoes ('write' kept as a switch)
p.SeriesGains = [1 1];         % contract 4: both engines audible in DUAL SERIES
p.ParallelNorm = 0.5;          % contract 4: peak-safe when echoes coincide
p.SingleE2Input = 'zero';      % contract 4: E2 keeps running and decays ('hold' kept as a switch)
% Front-panel controls (contract 5). Config: 1 SINGLE, 2 DUAL_SERIES, 3 DUAL_PARALLEL.
% FeedbackE2 = NaN means linked to Feedback (SHIFT+FEEDBACK unlinks it).
p.Ctl = struct('Config', 1, 'Time', 0.6, 'RatioIndex', 6, 'Feedback', 0.5, ...
               'FeedbackE2', NaN, 'Color', 0.3, 'Mix', 0.5);
p.E = struct('DelaySamples', {1, 1}, 'Feedback', {0, 0}, 'InputSend', {1, 1}, ...
             'LowCutHz', {0, 0}, 'HighCutHz', {0.49 * p.Fs, 0.49 * p.Fs});
p = mdd_controls_to_params(p.Ctl, p);
end
