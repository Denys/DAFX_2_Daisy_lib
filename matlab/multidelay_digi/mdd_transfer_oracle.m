function [b, a] = mdd_transfer_oracle(p)
% MDD_TRANSFER_ORACLE  Rational transfer function Y/X of the DIGI graph (integer reader,
%   SeriesFeedPoint 'tap'), independent of the per-sample kernel:
%     C = HP*LP, Hk = send_k z^-Dk / (1 - fb_k C_k z^-Dk)
%     SINGLE Dry + Wet*H1 ; SERIES Dry + Wet*(g1 H1 + g2 H1 H2) ; PARALLEL Dry + Wet*N*(H1 + H2)
sp = mdd_sanitize_params(p);
if sp.ReaderId ~= 1 || strcmp(sp.SeriesFeedPoint, 'write')
  error('mdd:oracle', 'oracle covers the integer reader with SeriesFeedPoint = tap only');
end
guard = 0.49 * sp.Fs; bk = cell(1, 2); ak = cell(1, 2);
for e = 1:2
  E = sp.E(e); cn = 1; cd = 1;
  if E.LowCutHz > 0
    q = exp(-2 * pi * E.LowCutHz / sp.Fs); cn = conv(cn, q * [1 -1]); cd = conv(cd, [1 -q]);
  end
  if E.HighCutHz < guard
    al = 1 - exp(-2 * pi * E.HighCutHz / sp.Fs); cn = conv(cn, al); cd = conv(cd, [1 -(1 - al)]);
  end
  zD = [zeros(1, floor(E.DelaySamples + 0.5)) 1];
  bk{e} = E.InputSend * conv(zD, cd);
  ak{e} = padd(cd, -E.Feedback * conv(cn, zD));
end
G = [1 0; sp.SeriesGains; sp.ParallelNorm * [1 1]]; g = G(sp.Config, :);
switch sp.Config
  case 1
    a = ak{1}; b = padd(sp.Dry * a, sp.Wet * g(1) * bk{1});
  case 2
    a = conv(ak{1}, ak{2});
    b = padd(sp.Dry * a, sp.Wet * padd(g(1) * conv(bk{1}, ak{2}), g(2) * conv(bk{1}, bk{2})));
  case 3
    a = conv(ak{1}, ak{2});
    b = padd(sp.Dry * a, sp.Wet * g(1) * padd(conv(bk{1}, ak{2}), conv(bk{2}, ak{1})));
end
end

function c = padd(u, v)
n = max(numel(u), numel(v));
c = [u zeros(1, n - numel(u))] + [v zeros(1, n - numel(v))];
end
