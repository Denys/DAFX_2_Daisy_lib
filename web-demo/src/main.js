/**
 * DAFX Web Demo - Main Application
 * Interactive browser-based demo of DSP effects using WebAudio
 */

// Effect parameter definitions
const EFFECT_PARAMS = {
    tube: [
        { name: 'drive', label: 'Drive', min: 0.1, max: 10, default: 2, step: 0.1 },
        { name: 'bias', label: 'Bias', min: -1, max: 1, default: 0, step: 0.01 },
        { name: 'distortion', label: 'Distortion', min: 0.1, max: 10, default: 1, step: 0.1 },
        { name: 'mix', label: 'Mix', min: 0, max: 1, default: 1, step: 0.01 }
    ],
    wahwah: [
        { name: 'frequency', label: 'Frequency', min: 200, max: 2000, default: 500, step: 10 },
        { name: 'depth', label: 'Depth', min: 0, max: 1, default: 0.7, step: 0.01 },
        { name: 'rate', label: 'Rate', min: 0.1, max: 10, default: 2, step: 0.1 }
    ],
    ringmod: [
        { name: 'frequency', label: 'Mod Freq', min: 50, max: 2000, default: 440, step: 1 },
        { name: 'depth', label: 'Depth', min: 0, max: 1, default: 1, step: 0.01 }
    ],
    vibrato: [
        { name: 'rate', label: 'Rate', min: 0.1, max: 15, default: 5, step: 0.1 },
        { name: 'depth', label: 'Depth', min: 0, max: 1, default: 0.5, step: 0.01 }
    ],
    noisegate: [
        { name: 'threshold', label: 'Threshold', min: -80, max: 0, default: -40, step: 1 },
        { name: 'attack', label: 'Attack', min: 0.001, max: 0.5, default: 0.01, step: 0.001 },
        { name: 'release', label: 'Release', min: 0.01, max: 1, default: 0.1, step: 0.01 }
    ],
    robotization: [
        { name: 'hopSize', label: 'Hop Size', min: 64, max: 512, default: 256, step: 64 }
    ],
    whisperization: [
        { name: 'hopSize', label: 'Hop Size', min: 32, max: 256, default: 64, step: 32 }
    ],
    phasevocoder: [
        { name: 'pitchRatio', label: 'Pitch', min: 0.5, max: 2, default: 1, step: 0.01 }
    ],
    reverb: [
        { name: 'rt60', label: 'Decay (RT60)', min: 0.1, max: 10, default: 2, step: 0.1 },
        { name: 'damping', label: 'Damping', min: 0, max: 1, default: 0.5, step: 0.01 },
        { name: 'mix', label: 'Mix', min: 0, max: 1, default: 0.3, step: 0.01 }
    ],
    stereopan: [
        { name: 'pan', label: 'Pan', min: -1, max: 1, default: 0, step: 0.01 }
    ]
};

// Application state
let audioContext = null;
let sourceNode = null;
let gainNode = null;
let analyserNode = null;
let workletNode = null;
let currentEffect = 'tube';
let inputSource = 'mic';
let mediaStream = null;
let oscillatorNode = null;

// DOM Elements
const startBtn = document.getElementById('startBtn');
const micBtn = document.getElementById('micBtn');
const fileBtn = document.getElementById('fileBtn');
const oscBtn = document.getElementById('oscBtn');
const audioFile = document.getElementById('audioFile');
const oscControls = document.getElementById('oscControls');
const oscFreq = document.getElementById('oscFreq');
const freqValue = document.getElementById('freqValue');
const effectsList = document.getElementById('effectsList');
const paramsContainer = document.getElementById('paramsContainer');
const waveformCanvas = document.getElementById('waveformCanvas');
const spectrumCanvas = document.getElementById('spectrumCanvas');
const outputVolume = document.getElementById('outputVolume');
const volumeValue = document.getElementById('volumeValue');
const bypassToggle = document.getElementById('bypassToggle');

// Initialize audio context
async function initAudio() {
    if (audioContext) return;

    try {
        audioContext = new AudioContext({ sampleRate: 48000 });

        // Create nodes
        gainNode = audioContext.createGain();
        gainNode.gain.value = 0.8;

        analyserNode = audioContext.createAnalyser();
        analyserNode.fftSize = 2048;

        // For now, use built-in effects as placeholder (WASM will replace this)
        // Connect: source -> effect -> gain -> analyser -> destination
        gainNode.connect(analyserNode);
        analyserNode.connect(audioContext.destination);

        // Start visualizers
        drawWaveform();
        drawSpectrum();

        startBtn.textContent = '⏹ Stop Audio';
        startBtn.classList.add('active');

        // Set up input source
        await setupInputSource();

    } catch (err) {
        console.error('Failed to initialize audio:', err);
        alert('Failed to initialize audio: ' + err.message);
    }
}

// Stop audio
function stopAudio() {
    if (sourceNode) {
        sourceNode.disconnect();
        if (sourceNode.stop) sourceNode.stop();
        sourceNode = null;
    }
    if (oscillatorNode) {
        oscillatorNode.stop();
        oscillatorNode = null;
    }
    if (mediaStream) {
        mediaStream.getTracks().forEach(track => track.stop());
        mediaStream = null;
    }
    if (audioContext) {
        audioContext.close();
        audioContext = null;
    }

    startBtn.textContent = '▶ Start Audio';
    startBtn.classList.remove('active');
}

// Set up input source
async function setupInputSource() {
    // Disconnect existing source
    if (sourceNode) {
        sourceNode.disconnect();
        if (sourceNode.stop) sourceNode.stop();
        sourceNode = null;
    }
    if (oscillatorNode) {
        oscillatorNode.stop();
        oscillatorNode = null;
    }

    switch (inputSource) {
        case 'mic':
            try {
                mediaStream = await navigator.mediaDevices.getUserMedia({ audio: true });
                sourceNode = audioContext.createMediaStreamSource(mediaStream);
                sourceNode.connect(gainNode);
            } catch (err) {
                console.error('Microphone access denied:', err);
                alert('Microphone access denied. Please allow microphone access and try again.');
            }
            break;

        case 'oscillator':
            oscillatorNode = audioContext.createOscillator();
            oscillatorNode.type = 'sine';
            oscillatorNode.frequency.value = parseInt(oscFreq.value);
            oscillatorNode.connect(gainNode);
            oscillatorNode.start();
            break;
    }
}

// Update effect parameters UI
function updateParamsUI() {
    const params = EFFECT_PARAMS[currentEffect] || [];
    paramsContainer.innerHTML = '';

    params.forEach(param => {
        const row = document.createElement('div');
        row.className = 'param-row';

        const displayValue = param.default.toFixed(param.step < 1 ? 2 : 0);

        row.innerHTML = `
      <label>${param.label}</label>
      <input type="range" 
             id="param-${param.name}" 
             min="${param.min}" 
             max="${param.max}" 
             value="${param.default}" 
             step="${param.step}"
             data-param="${param.name}">
      <span class="value" id="value-${param.name}">${displayValue}</span>
    `;

        paramsContainer.appendChild(row);

        // Add event listener
        const slider = row.querySelector('input');
        slider.addEventListener('input', (e) => {
            const value = parseFloat(e.target.value);
            const valueSpan = document.getElementById(`value-${param.name}`);
            valueSpan.textContent = value.toFixed(param.step < 1 ? 2 : 0);

            // Send parameter to worklet (when WASM is ready)
            if (workletNode) {
                workletNode.port.postMessage({
                    type: 'setParam',
                    effect: currentEffect,
                    param: param.name,
                    value: value
                });
            }
        });
    });
}

// Draw waveform visualizer
function drawWaveform() {
    if (!analyserNode) return;

    const canvas = waveformCanvas;
    const ctx = canvas.getContext('2d');
    const width = canvas.width;
    const height = canvas.height;

    const bufferLength = analyserNode.frequencyBinCount;
    const dataArray = new Uint8Array(bufferLength);

    function draw() {
        requestAnimationFrame(draw);

        analyserNode.getByteTimeDomainData(dataArray);

        ctx.fillStyle = '#0d1117';
        ctx.fillRect(0, 0, width, height);

        ctx.lineWidth = 2;
        ctx.strokeStyle = '#58a6ff';
        ctx.beginPath();

        const sliceWidth = width / bufferLength;
        let x = 0;

        for (let i = 0; i < bufferLength; i++) {
            const v = dataArray[i] / 128.0;
            const y = (v * height) / 2;

            if (i === 0) {
                ctx.moveTo(x, y);
            } else {
                ctx.lineTo(x, y);
            }
            x += sliceWidth;
        }

        ctx.lineTo(width, height / 2);
        ctx.stroke();
    }

    draw();
}

// Draw spectrum visualizer
function drawSpectrum() {
    if (!analyserNode) return;

    const canvas = spectrumCanvas;
    const ctx = canvas.getContext('2d');
    const width = canvas.width;
    const height = canvas.height;

    const bufferLength = analyserNode.frequencyBinCount;
    const dataArray = new Uint8Array(bufferLength);

    function draw() {
        requestAnimationFrame(draw);

        analyserNode.getByteFrequencyData(dataArray);

        ctx.fillStyle = '#0d1117';
        ctx.fillRect(0, 0, width, height);

        const barWidth = (width / bufferLength) * 2.5;
        let x = 0;

        for (let i = 0; i < bufferLength; i++) {
            const barHeight = (dataArray[i] / 255) * height;

            const hue = (i / bufferLength) * 120 + 200;
            ctx.fillStyle = `hsl(${hue}, 70%, 50%)`;
            ctx.fillRect(x, height - barHeight, barWidth, barHeight);

            x += barWidth + 1;
            if (x > width) break;
        }
    }

    draw();
}

// Event Listeners
startBtn.addEventListener('click', () => {
    if (audioContext) {
        stopAudio();
    } else {
        initAudio();
    }
});

// Input source buttons
micBtn.addEventListener('click', async () => {
    inputSource = 'mic';
    micBtn.classList.add('active');
    fileBtn.classList.remove('active');
    oscBtn.classList.remove('active');
    oscControls.classList.add('hidden');
    if (audioContext) await setupInputSource();
});

fileBtn.addEventListener('click', () => {
    audioFile.click();
});

audioFile.addEventListener('change', async (e) => {
    if (e.target.files.length > 0) {
        inputSource = 'file';
        micBtn.classList.remove('active');
        fileBtn.classList.add('active');
        oscBtn.classList.remove('active');
        oscControls.classList.add('hidden');

        const file = e.target.files[0];
        const arrayBuffer = await file.arrayBuffer();

        if (!audioContext) await initAudio();

        const audioBuffer = await audioContext.decodeAudioData(arrayBuffer);

        if (sourceNode) {
            sourceNode.disconnect();
            if (sourceNode.stop) sourceNode.stop();
        }

        sourceNode = audioContext.createBufferSource();
        sourceNode.buffer = audioBuffer;
        sourceNode.loop = true;
        sourceNode.connect(gainNode);
        sourceNode.start();
    }
});

oscBtn.addEventListener('click', async () => {
    inputSource = 'oscillator';
    micBtn.classList.remove('active');
    fileBtn.classList.remove('active');
    oscBtn.classList.add('active');
    oscControls.classList.remove('hidden');
    if (audioContext) await setupInputSource();
});

oscFreq.addEventListener('input', (e) => {
    const freq = parseInt(e.target.value);
    freqValue.textContent = freq;
    if (oscillatorNode) {
        oscillatorNode.frequency.value = freq;
    }
});

// Effect selection
effectsList.addEventListener('click', (e) => {
    if (e.target.classList.contains('effect-item')) {
        document.querySelectorAll('.effect-item').forEach(el => el.classList.remove('active'));
        e.target.classList.add('active');
        currentEffect = e.target.dataset.effect;
        updateParamsUI();

        // Notify worklet of effect change
        if (workletNode) {
            workletNode.port.postMessage({
                type: 'setEffect',
                effect: currentEffect
            });
        }
    }
});

// Volume control
outputVolume.addEventListener('input', (e) => {
    const value = parseInt(e.target.value);
    volumeValue.textContent = value + '%';
    if (gainNode) {
        gainNode.gain.value = value / 100;
    }
});

// Bypass toggle
bypassToggle.addEventListener('change', (e) => {
    if (workletNode) {
        workletNode.port.postMessage({
            type: 'bypass',
            enabled: e.target.checked
        });
    }
});

// Initialize UI
updateParamsUI();
