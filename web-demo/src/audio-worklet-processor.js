/**
 * DAFX Audio Worklet Processor
 * Runs DSP effects in the audio thread
 */

class DAFXProcessor extends AudioWorkletProcessor {
    constructor() {
        super();

        this.currentEffect = 'tube';
        this.bypass = false;
        this.params = {};
        this.wasmReady = false;
        this.wasmModule = null;
        this.effectInstance = null;

        // Handle messages from main thread
        this.port.onmessage = (event) => {
            const { type, ...data } = event.data;

            switch (type) {
                case 'init':
                    this.initWasm(data.wasmModule);
                    break;
                case 'setEffect':
                    this.setEffect(data.effect);
                    break;
                case 'setParam':
                    this.setParam(data.effect, data.param, data.value);
                    break;
                case 'bypass':
                    this.bypass = data.enabled;
                    break;
            }
        };

        // Request WASM module from main thread
        this.port.postMessage({ type: 'requestWasm' });
    }

    initWasm(wasmModule) {
        if (wasmModule) {
            this.wasmModule = wasmModule;
            this.wasmReady = true;
            this.createEffectInstance();
        }
    }

    setEffect(effectName) {
        this.currentEffect = effectName;
        if (this.wasmReady) {
            this.createEffectInstance();
        }
    }

    createEffectInstance() {
        if (!this.wasmModule) return;

        // Create effect instance based on current effect
        // This will be implemented when WASM bindings are ready
        try {
            switch (this.currentEffect) {
                case 'tube':
                    this.effectInstance = new this.wasmModule.Tube();
                    this.effectInstance.Init(sampleRate);
                    break;
                case 'wahwah':
                    this.effectInstance = new this.wasmModule.WahWah();
                    this.effectInstance.Init(sampleRate);
                    break;
                // Add more effects as WASM bindings are created
            }
        } catch (err) {
            console.error('Failed to create effect instance:', err);
        }
    }

    setParam(effect, param, value) {
        if (!this.params[effect]) {
            this.params[effect] = {};
        }
        this.params[effect][param] = value;

        // Apply to WASM effect instance
        if (this.effectInstance && effect === this.currentEffect) {
            const setterName = 'Set' + param.charAt(0).toUpperCase() + param.slice(1);
            if (typeof this.effectInstance[setterName] === 'function') {
                this.effectInstance[setterName](value);
            }
        }
    }

    process(inputs, outputs, parameters) {
        const input = inputs[0];
        const output = outputs[0];

        if (!input || !input[0]) return true;

        // Process each channel
        for (let channel = 0; channel < output.length; channel++) {
            const inputChannel = input[channel] || input[0];
            const outputChannel = output[channel];

            if (this.bypass || !this.wasmReady || !this.effectInstance) {
                // Pass-through when bypassed or WASM not ready
                outputChannel.set(inputChannel);
            } else {
                // Process through WASM effect
                for (let i = 0; i < outputChannel.length; i++) {
                    outputChannel[i] = this.effectInstance.Process(inputChannel[i]);
                }
            }
        }

        return true;
    }
}

registerProcessor('dafx-processor', DAFXProcessor);
