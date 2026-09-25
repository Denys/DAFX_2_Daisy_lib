function [v, y1] = mdd_read(buf, col, w, cap, D, rid, y1)
% MDD_READ  History readers (layer 2). h[k] = sample written k frames ago, k >= 1;
%   w is the slot not yet written this frame, so h[k] = buf(bitand(w-1-k+cap, cap-1)+1).
%   rid: 1 integer, 2 linear, 3 allpass1, 4 lagrange3. y1 is the allpass state.
m = cap - 1; b = w - 1 + cap;
switch rid
  case 1                                     % round half up, as the node
    v = buf(bitand(b - floor(D + 0.5), m) + 1, col);
  case 2
    M = floor(D); f = cast(D - M, class(buf));
    v = (1 - f) * buf(bitand(b - M, m) + 1, col) + f * buf(bitand(b - M - 1, m) + 1, col);
  case 3                                     % first-order allpass, delta in [0.5, 1.5)
    M = floor(D - 0.5); d = cast(D - M, class(buf));
    eta = (1 - d) / (1 + d);
    v = eta * buf(bitand(b - M, m) + 1, col) + buf(bitand(b - M - 1, m) + 1, col) - eta * y1;
    y1 = v;
  case 4                                     % third-order Lagrange, taps M-1 ... M+2
    M = floor(D); f = cast(D - M, class(buf));
    cm1 = -f * (f - 1) * (f - 2) / 6;
    c0 = (f + 1) * (f - 1) * (f - 2) / 2;
    c1 = -(f + 1) * f * (f - 2) / 2;
    c2 = (f + 1) * f * (f - 1) / 6;
    v = cm1 * buf(bitand(b - M + 1, m) + 1, col) + c0 * buf(bitand(b - M, m) + 1, col) ...
      + c1 * buf(bitand(b - M - 1, m) + 1, col) + c2 * buf(bitand(b - M - 2, m) + 1, col);
end
end
