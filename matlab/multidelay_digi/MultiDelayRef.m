classdef MultiDelayRef < matlab.System
% MULTIDELAYREF  matlab.System wrapper around the mono-first DIGI kernel (MATLAB only).
%   obj = MultiDelayRef('Params', mdd_default_params()); y = obj(x);
%   No DSP here: stepImpl calls mdd_process_block, so kernel tests cover the algorithm.
%   Structural fields (Fs, MaxDelayMs, Reader, Precision, BlockSize) are fixed after setup;
%   the rest of Params may change between steps (one snapshot per step).
  properties
    Params = mdd_default_params();
  end
  properties (Access = private)
    State
    Fixed
  end
  methods
    function obj = MultiDelayRef(varargin)
      setProperties(obj, nargin, varargin{:});
    end
    function d = diagnostics(obj)
      d = obj.State.diag;
    end
  end
  methods (Access = protected)
    function setupImpl(obj, ~)
      p = obj.Params;
      obj.Fixed = {p.Fs, p.MaxDelayMs, p.Reader, p.Precision, p.BlockSize};
      obj.State = mdd_init_state(p);
    end
    function y = stepImpl(obj, x)
      p = obj.Params;
      if ~isequal({p.Fs, p.MaxDelayMs, p.Reader, p.Precision, p.BlockSize}, obj.Fixed)
        error('MultiDelayRef:nontunable', 'Fs, MaxDelayMs, Reader, Precision and BlockSize are fixed after setup');
      end
      [y, obj.State] = mdd_process_block(x, p, obj.State);
    end
    function resetImpl(obj)
      obj.State = mdd_init_state(obj.Params);
    end
  end
end
