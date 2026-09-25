function st = mdd_init_state(p)
% MDD_INIT_STATE  Zeroed histories, write index, filter/allpass/smoother state, counters.
%   One history column per engine. Capacity = next power of two >= max delay + 4, so the
%   lagrange3 reader's h[M+2] stays inside the ring and wrapping is a mask.
sp = mdd_sanitize_params(p);
st.cap = 2 ^ nextpow2(sp.MaxDelaySamples + 4);
st.buf = zeros(st.cap, 2, sp.Class);
st.w = 1;                                  % slot written at the current frame (1-based)
z = zeros(1, 2, sp.Class);
st.hp_x1 = z; st.hp_y1 = z; st.lp_y = z; st.ap_y1 = z;
st.Dcur = [NaN NaN];                       % smoothed delay; NaN = start at target
st.diag = struct('non_finite_samples', 0, 'max_abs_sample', 0, 'clipped_samples', 0, ...
                 'processed_blocks', 0, 'processed_samples', 0);
end
