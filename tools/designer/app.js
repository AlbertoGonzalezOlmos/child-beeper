"use strict";

const $ = id => document.getElementById(id);
const W = 172, H = 320;
const SCENES = ["home", "message", "alarm"];
const BUILTIN_FONTS = {
  montserrat14: "Sans · 14 px", montserrat20: "Sans · 20 px",
  montserrat28: "Sans · 28 px", montserrat32: "Sans · 32 px",
  mono8: "Mono · 8 px", mono16: "Mono · 16 px"
};
const BINDING_NAMES = {
  static: "Custom text", clock: "Time", date: "Date", alarm: "Alarm status",
  message: "Latest message", reply: "Selected reply", hint: "Button hints",
  network: "Connection & pairing code", microphone: "Microphone level"
};
const ACTION_NAMES = {
  nextReply: "Next reply / change value", sendReply: "Send reply / confirm",
  acknowledge: "Mark read / stop alarm", editAlarm: "Open alarm settings"
};
const state = {
  project: null, defaults: null, token: "", scene: "home", panel: "layout", selected: null,
  playing: true, time: 0, lastTime: performance.now(), duration: 2000, zoom: 1.75,
  dirty: false, revision: 0, submittedRevision: null, busy: false, pending: false,
  undo: [], redo: [], fonts: new Map(), images: new Map(), loading: new Map(),
  previewMessage: "Hey! I'll pick you up at four.\nSee you soon ♥", microphone: 35,
  audioOn: false, audio: null, drag: null, assetEditing: null, toastTimer: null,
  lastStatus: "", jobSeenBusy: false, pins: [], portList: [], continuous: null
};
const canvas = $("screen"), ctx = canvas.getContext("2d");
ctx.imageSmoothingEnabled = false;

function escapeHTML(value) {
  return String(value).replace(/[&<>"']/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
}
const h = escapeHTML;
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
const selected = () => state.project?.layers.find(layer => layer.id === state.selected);
const sceneLayers = () => state.project.layers.filter(layer => layer.scene === state.scene);
const trigger = () => state.scene === "home" ? "idle" : state.scene;
const uid = prefix => prefix + "_" + crypto.randomUUID().replaceAll("-", "").slice(0, 12);
const fontLabel = id => BUILTIN_FONTS[id] || `${state.project.assets.find(a => a.id === id)?.name || id} · ${state.project.assets.find(a => a.id === id)?.size || ""} px`;

async function api(path, body) {
  const options = body === undefined ? {} : {
    method: "POST", headers: { "Content-Type": "application/json", "X-Designer-Token": state.token },
    body: JSON.stringify(body)
  };
  const response = await fetch(path, options);
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || `Request failed (${response.status}).`);
  return data;
}

function toast(message, error = false) {
  clearTimeout(state.toastTimer);
  $("toast").textContent = message;
  $("toast").classList.toggle("error", error);
  $("toast").hidden = false;
  state.toastTimer = setTimeout(() => { $("toast").hidden = true; }, error ? 8500 : 4200);
}
function handleError(error) { console.error(error); toast(error.message || String(error), true); }
function bind(id, event, handler) {
  $(id).addEventListener(event, e => {
    try { Promise.resolve(handler(e)).catch(handleError); } catch (error) { handleError(error); }
  });
}

function snapshot() {
  const { assets, ...rest } = state.project;
  return { ...structuredClone(rest), assets: assets.map(asset => ({ ...asset })) };
}
function remember() {
  state.undo.push(snapshot());
  if (state.undo.length > 30) state.undo.shift();
  state.redo = [];
}
function changed() {
  state.dirty = true;
  state.revision++;
  $("dirty").textContent = "Unsaved changes";
  $("status-text").textContent = "Design changes stay local until you save, build or flash.";
  updateTimeline();
  updateButtons();
}
function undo(redo = false) {
  const source = redo ? state.redo : state.undo, target = redo ? state.undo : state.redo;
  if (!source.length) return;
  target.push(snapshot());
  state.project = source.pop();
  if (!selected()) state.selected = sceneLayers()[0]?.id;
  changed();
  renderAll();
  loadAssets();
}
function options(values, value) {
  return values.map(([key, label]) => `<option value="${h(key)}"${key === value ? " selected" : ""}>${h(label)}</option>`).join("");
}
function field(label, control) { return `<div class="field"><label>${h(label)}</label>${control}</div>`; }
function numberField(label, key, value, min, max, scope = "layer") {
  return field(label, `<input type="number" data-${scope}="${h(key)}" value="${value}" min="${min}" max="${max}" step="1">`);
}
function layerSelect(label, key, value, choices) {
  return field(label, `<select data-layer="${key}">${options(choices, value)}</select>`);
}
function colorControl(value, attribute) {
  return `<div class="color-control"><input type="color" ${attribute} value="${h(value)}"><code>${h(value.toUpperCase())}</code></div>`;
}
function colorField(label, key, value, scope = "layer") {
  return field(label, colorControl(value, `data-${scope}="${key}"`));
}

function renderLayers() {
  const layers = sceneLayers();
  $("layer-count").textContent = `${layers.length} layers`;
  $("layers").innerHTML = [...layers].reverse().map(layer => `
    <div class="layer ${layer.id === state.selected ? "selected" : ""}" data-layer-id="${h(layer.id)}" tabindex="0" role="button" aria-label="Select ${h(layer.name)}">
      <span class="layer-symbol">${({ text: "T", box: "□", ellipse: "○", image: "▧" })[layer.kind]}</span>
      <span class="layer-name">${h(layer.name)}</span>
      <span class="layer-kind">${layer.animation.keys.length ? "motion" : layer.kind === "text" && layer.binding !== "static" ? "live" : ""}</span>
    </div>`).join("");
}

function renderAssets() {
  $("assets").innerHTML = state.project.assets.length ? state.project.assets.map(asset => {
    const preview = state.images.get(asset.id);
    return `<div class="asset" data-asset="${h(asset.id)}" tabindex="0" role="button" aria-label="Edit ${h(asset.name)}">
      <span class="asset-thumb">${asset.kind === "font" ? "Aa" : preview?.frames[0] ? `<img alt="" src="${h(preview.frames[0].src)}">` : "▧"}</span>
      <span class="asset-info"><strong>${h(asset.name)}</strong><small>${asset.kind === "font" ? `Font · ${asset.size} px` : `${asset.width} × ${asset.height}${preview ? ` · ${preview.frames.length} frame${preview.frames.length === 1 ? "" : "s"}` : " · loading"}`}</small></span>
      ${asset.kind === "image" ? `<button data-insert-asset="${h(asset.id)}" title="Add image to this scene" aria-label="Add ${h(asset.name)} to screen">＋</button>` : ""}
    </div>`;
  }).join("") : `<p class="empty-note">Import images or animated GIFs with the Image button. Fonts can be TTF or OTF.</p>`;
}

function renderLayout() {
  const layer = selected();
  let html = `<h3>Screen</h3>${colorField("Background", "background", state.project.scenes[state.scene].background, "scene")}<hr>`;
  if (!layer) {
    html += `<p class="help">Select a layer on the screen or add one from the left.</p>`;
  } else {
    const fontChoices = [...Object.keys(BUILTIN_FONTS), ...state.project.assets.filter(a => a.kind === "font").map(a => a.id)].map(id => [id, fontLabel(id)]);
    html += `<h3>Selected layer</h3>
      ${field("Name", `<input data-layer="name" value="${h(layer.name)}" maxlength="80">`)}
      ${layerSelect("Type", "kind", layer.kind, [["text", "Text"], ["box", "Rectangle"], ["ellipse", "Ellipse"], ["image", "Image / GIF"]])}
      <div class="field-row">${numberField("X", "x", layer.x, -W, W)}${numberField("Y", "y", layer.y, -H, H)}</div>
      <div class="field-row">${numberField("Width", "width", layer.width, 1, W)}${numberField("Height", "height", layer.height, 1, H)}</div>`;
    if (layer.kind === "text") {
      html += layerSelect("Content", "binding", layer.binding, Object.entries(BINDING_NAMES));
      if (layer.binding === "static") html += field("Text", `<textarea data-layer="text" maxlength="512">${h(layer.text)}</textarea>`);
      html += layerSelect("Font", "font", layer.font, fontChoices);
      html += layerSelect("Alignment", "align", layer.align, [["left", "Left"], ["center", "Center"], ["right", "Right"]]);
      html += `<label class="checkbox-field"><input type="checkbox" data-layer="scroll"${layer.scroll ? " checked" : ""}> Scroll overflowing text</label>`;
    }
    if (layer.kind === "image") {
      html += layerSelect("Image / animation", "asset", layer.asset,
        state.project.assets.filter(a => a.kind === "image").map(a => [a.id, a.name]));
      html += `<p class="help">GIF timing and transparency are preserved. The device plays up to 20 frames per second.</p>`;
    } else html += colorField(layer.kind === "text" ? "Text color" : "Fill color", "color", layer.color);
    html += `<div class="field"><label>Opacity <span id="opacity-label">${Math.round(layer.opacity / 255 * 100)}%</span></label>
      <input type="range" data-layer="opacity" value="${layer.opacity}" min="0" max="255"></div><hr>
      <h3>Animation</h3>
      ${field("Quick motion", `<select id="motion-preset"><option value="">Choose a preset…</option><option value="none">None</option><option value="fade">Fade in &amp; out</option><option value="pulse">Pulse opacity</option><option value="slideX">Slide horizontally</option><option value="slideY">Float vertically</option></select>`)}
      ${numberField("Loop duration (ms)", "period", layer.animation.period, 100, 60000, "animation")}
      <p class="help">Move a layer to shift its whole path. Edit keyframes to shape its movement.</p>
      <div class="keyframe-heading"><span>Time ms</span><span>X</span><span>Y</span><span>Opacity</span><span></span></div>
      ${layer.animation.keys.map((key, i) => `<div class="keyframe" data-key-index="${i}">
        <input aria-label="Keyframe ${i + 1} time" data-key="at" type="number" value="${key.at}" min="0" max="${layer.animation.period}"${i === 0 ? " readonly" : ""}>
        <input aria-label="Keyframe ${i + 1} x" data-key="x" type="number" value="${key.x}" min="-172" max="172">
        <input aria-label="Keyframe ${i + 1} y" data-key="y" type="number" value="${key.y}" min="-320" max="320">
        <input aria-label="Keyframe ${i + 1} opacity" data-key="opacity" type="number" value="${key.opacity}" min="0" max="255">
        <button data-delete-key="${i}" aria-label="Delete keyframe ${i + 1}"${i === 0 && layer.animation.keys.length > 1 ? " disabled" : ""}>×</button>
      </div>`).join("")}
      <button id="add-key" class="wide quiet">＋ Keyframe at playhead</button>`;
  }
  html += `<hr><div class="preview-inputs"><h3>PREVIEW CONTENT</h3>
    ${field("Sample incoming message", `<textarea id="preview-message" maxlength="240">${h(state.previewMessage)}</textarea>`)}
    <div class="field"><label>Simulated microphone level · ${state.microphone}%</label><input id="preview-mic" type="range" min="0" max="100" value="${state.microphone}"></div>
    <p class="help">These samples are only used in the editor. The device shows its live data.</p></div>`;
  $("inspector").innerHTML = html;
}

function renderSignals() {
  const effect = state.project.effects[trigger()];
  $("inspector").innerHTML = `<h3>${state.scene === "home" ? "Idle" : state.scene === "message" ? "Message" : "Alarm"} signal</h3>
    <p class="help">Combine light, vibration, tones and screen flashes. The alarm stops after 60 seconds or when acknowledged.</p>
    ${field("Start with a pattern", `<select id="signal-preset"><option value="">Choose a preset…</option><option value="heartbeat">Blue / yellow heartbeat</option><option value="rainbow">Rainbow transition</option><option value="alarm">Light, sound &amp; vibration alarm</option><option value="quiet">Quiet blue</option></select>`)}
    <label class="checkbox-field"><input id="effect-loop" type="checkbox"${effect.loop ? " checked" : ""}> Loop this sequence</label>
    <p class="help">Sound and vibration are designed here. Enable their hardware channels when the parts are connected.</p>
    <div id="steps">${effect.steps.map((step, i) => `
      <div class="signal-card" data-step-index="${i}">
        <div class="signal-card-head"><span class="step-dot" style="background:${h(step.color)}"></span><b>Step ${i + 1}</b>
          <button data-move-step="${i}" data-direction="-1" title="Move step earlier"${i === 0 ? " disabled" : ""}>↑</button>
          <button data-move-step="${i}" data-direction="1" title="Move step later"${i === effect.steps.length - 1 ? " disabled" : ""}>↓</button>
          <button data-delete-step="${i}" title="Delete step"${effect.steps.length === 1 ? " disabled" : ""}>×</button></div>
        <div class="field-row">${numberField("Duration · ms", "duration", step.duration, 20, 5000, "step")}${colorField("LED", "color", step.color, "step")}</div>
        <div class="field-row">${numberField("LED · %", "brightness", step.brightness, 0, 100, "step")}${numberField("Vibration · %", "vibration", step.vibration, 0, 100, "step")}</div>
        <div class="field-row">${numberField("Tone · Hz", "frequency", step.frequency, 0, 6000, "step")}${numberField("Volume · %", "volume", step.volume, 0, 100, "step")}</div>
        ${field("Waveform", `<select data-step="wave">${options([["sine", "Sine · soft"], ["triangle", "Triangle · rounded"], ["square", "Square · bright"]], step.wave)}</select>`)}
        <label class="checkbox-field"><input data-step="blend" type="checkbox"${step.blend ? " checked" : ""}> Smooth transition to next step</label>
        <label class="checkbox-field"><input data-step="screen" type="checkbox"${step.screen ? " checked" : ""}> Override screen background</label>
        ${step.screen ? colorField("Screen color", "screenColor", step.screenColor, "step") : ""}
      </div>`).join("")}</div>
      <button id="add-step" class="wide">＋ Add signal step</button>
      <p class="help">${effect.steps.length} / 32 steps · ${(effect.steps.reduce((sum, s) => sum + s.duration, 0) / 1000).toFixed(2)} seconds. Maximum sequence length: 60 seconds.</p>`;
}

function pinSelect(label, property, value, scope) {
  return field(label, `<select data-hardware="${property}" data-scope="${scope}">${options(state.pins.map(pin => [String(pin), `GPIO ${pin}`]), String(value))}</select>`);
}
function hardwareError() {
  const hardware = state.project.hardware, used = new Map();
  const routes = [];
  hardware.buttons.forEach((b, i) => { if (b.enabled) routes.push([b.gpio, `button ${i + 1}`]); });
  if (hardware.motor.enabled) routes.push([hardware.motor.gpio, "motor"]);
  const audio = hardware.audio;
  if (audio.speaker || audio.microphone) routes.push([audio.bclk, "audio clock"], [audio.ws, "audio word-select"]);
  if (audio.speaker) routes.push([audio.dout, "speaker data"]);
  if (audio.microphone) routes.push([audio.din, "microphone data"]);
  for (const [pin, role] of routes) {
    if (!state.pins.includes(pin)) return `GPIO ${pin} is unavailable for ${role}.`;
    if (used.has(pin)) return `GPIO ${pin} is assigned to both ${used.get(pin)} and ${role}. Choose separate pins.`;
    used.set(pin, role);
  }
  return "";
}
function renderHardware() {
  const hardware = state.project.hardware, audio = hardware.audio;
  $("inspector").innerHTML = `<h3>Future hardware</h3>
    <div class="hardware-note"><strong>3.3 V logic · shared ground</strong>
      GPIOs control external modules. Use an I²S amplifier for a speaker and a MOSFET/driver for a motor. Never wire a raw speaker or motor directly to a GPIO.</div>
    <div id="hardware-error"></div>
    <h3>External buttons</h3>
    ${hardware.buttons.map((button, i) => `<div class="hardware-card">
      <label class="checkbox-field"><input data-hardware="enabled" data-scope="button${i}" type="checkbox"${button.enabled ? " checked" : ""}> Button ${i + 1}</label>
      ${pinSelect("Input pin", "gpio", button.gpio, `button${i}`)}
      ${field("Action", `<select data-hardware="action" data-scope="button${i}">${options(Object.entries(ACTION_NAMES), button.action)}</select>`)}
      <label class="checkbox-field"><input data-hardware="activeLow" data-scope="button${i}" type="checkbox"${button.activeLow ? " checked" : ""}> Active low · switch to ground</label>
    </div>`).join("")}
    <h3>Vibration motor</h3>
    <div class="hardware-card">
      <label class="checkbox-field"><input data-hardware="enabled" data-scope="motor" type="checkbox"${hardware.motor.enabled ? " checked" : ""}> Enable motor output</label>
      ${pinSelect("Driver control pin", "gpio", hardware.motor.gpio, "motor")}
      <p class="help">PWM output for a DC/ERM motor driver. Use a suitable motor supply, flyback protection and a gate pulldown. An LRA needs its own controller.</p>
    </div>
    <h3>Speaker &amp; microphone</h3>
    <div class="hardware-card">
      <label class="checkbox-field"><input data-hardware="speaker" data-scope="audio" type="checkbox"${audio.speaker ? " checked" : ""}> I²S amplifier / speaker</label>
      <label class="checkbox-field"><input data-hardware="microphone" data-scope="audio" type="checkbox"${audio.microphone ? " checked" : ""}> I²S digital microphone</label>
      <div class="field-row">${pinSelect("BCLK · shared", "bclk", audio.bclk, "audio")}${pinSelect("WS · shared", "ws", audio.ws, "audio")}</div>
      ${pinSelect("Speaker data out", "dout", audio.dout, "audio")}
      ${pinSelect("Microphone data in", "din", audio.din, "audio")}
      <p class="help">16 kHz Philips I²S, 32-bit stereo slots. MAX98357A-style amplifiers and INMP441-style microphones are suitable. Select the microphone's left channel. Both modules share BCLK and WS.</p>
    </div>
    <p class="help">Only spare header GPIOs are offered. Display, USB, flash, SD, battery, RGB, IMU and boot-strapping pins are protected. External hardware still needs electrical testing when connected.</p>`;
  renderHardwareState();
}

function renderHardwareState() {
  const error = hardwareError();
  const target = $("hardware-error");
  if (target) target.innerHTML = error ? `<div class="hardware-error">${h(error)}</div>` : "";
  const hardware = state.project.hardware;
  const enabled = hardware.buttons.some(b => b.enabled) || hardware.motor.enabled || hardware.audio.speaker || hardware.audio.microphone;
  $("output-status").textContent = error ? "GPIO conflict" : enabled ? "External hardware configured" : "External outputs off";
}
function renderInspector() {
  document.querySelectorAll("[data-panel]").forEach(button => button.classList.toggle("active", button.dataset.panel === state.panel));
  if (state.panel === "signals") renderSignals();
  else if (state.panel === "hardware") renderHardware();
  else renderLayout();
}
function renderAll() {
  $("project-name").value = state.project.name;
  $("scene-title").textContent = `${state.scene[0].toUpperCase()}${state.scene.slice(1)} screen`;
  document.querySelectorAll("[data-scene]").forEach(button => button.classList.toggle("active", button.dataset.scene === state.scene));
  renderLayers(); renderAssets(); renderInspector(); updateTimeline(); updateButtons(); renderHardwareState();
}
function updateButtons() {
  const unavailable = !state.project || state.pending || state.busy;
  $("save-project").disabled = unavailable;
  $("build-project").disabled = unavailable || !!(state.project && hardwareError());
  $("flash-project").disabled = unavailable || !$("port").value || !!(state.project && hardwareError());
  $("undo").disabled = !state.undo.length;
  $("redo").disabled = !state.redo.length;
  for (const id of ["layer-up", "layer-down", "duplicate-layer", "delete-layer"]) $(id).disabled = !selected();
}
function updateTimeline() {
  if (!state.project) return;
  const effect = state.project.effects[trigger()];
  let duration = Math.max(2000, effect.steps.reduce((sum, step) => sum + step.duration, 0));
  for (const layer of sceneLayers()) {
    if (layer.animation.keys.length) duration = Math.max(duration, layer.animation.period);
    const image = state.images.get(layer.asset);
    if (image) duration = Math.max(duration, image.durations.reduce((a, b) => a + b, 0));
  }
  state.duration = Math.min(60000, duration);
  $("scrubber").max = state.duration;
  $("duration-label").textContent = `${(state.duration / 1000).toFixed(state.duration % 1000 ? 2 : 0)} s`;
  $("effect-strip").innerHTML = effect.steps.map(step => `<div style="flex:${step.duration};background:${h(step.color)}" title="${step.duration} ms · ${step.frequency} Hz · vibration ${step.vibration}%"></div>`).join("");
}

async function imageFromURL(source) {
  const image = new Image();
  image.src = source;
  await image.decode();
  return image;
}
async function loadFont(name) {
  const data = await api(`/api/fonts/${name}`);
  data.image = await imageFromURL(data.data);
  data.tints = new Map();
  state.fonts.set(name, data);
}
function assetSpec(asset) {
  const texts = asset.kind === "font" ? state.project.layers.filter(l => l.font === asset.id).map(l => l.text).join("") : "";
  const { data, ...rest } = asset;
  return JSON.stringify(rest) + texts;
}
async function loadAsset(asset) {
  const spec = assetSpec(asset), source = asset.data;
  const cache = asset.kind === "font" ? state.fonts : state.images;
  const existing = cache.get(asset.id);
  if (existing?.source === source && existing.spec === spec) return;
  if (state.loading.has(asset.id)) return;
  state.loading.set(asset.id, spec);
  try {
    const data = await api("/api/asset-preview", { project: state.project, id: asset.id });
    const current = state.project.assets.find(a => a.id === asset.id);
    if (!current || current.data !== source || assetSpec(current) !== spec) return;
    if (asset.kind === "font") {
      data.image = await imageFromURL(data.data);
      data.tints = new Map();
    } else {
      data.durations = data.frames.map(frame => frame.duration);
      data.frames = await Promise.all(data.frames.map(frame => imageFromURL(frame.data)));
    }
    data.source = source;
    data.spec = spec;
    cache.set(asset.id, data);
    renderAssets();
    updateTimeline();
  } catch (error) {
    cache.set(asset.id, { source, spec, failed: true, frames: [], durations: [] });
    handleError(error);
  } finally {
    state.loading.delete(asset.id);
    const current = state.project.assets.find(a => a.id === asset.id);
    if (current && (current.data !== source || assetSpec(current) !== spec)) loadAsset(current);
  }
}
function loadAssets() {
  state.project.assets.forEach(asset => loadAsset(asset));
}

function pose(layer, time) {
  let result = { x: layer.x, y: layer.y, opacity: layer.opacity };
  const keys = layer.animation.keys;
  if (!keys.length) return result;
  const at = time % layer.animation.period;
  let a = keys[0], b = a;
  for (let i = 1; i < keys.length; i++) { b = keys[i]; if (at < b.at) break; a = b; }
  const t = clamp(b.at > a.at ? Math.floor((at - a.at) * 1024 / (b.at - a.at)) : 0, 0, 1024);
  for (const prop of ["x", "y", "opacity"]) result[prop] = a[prop] + Math.trunc((b[prop] - a[prop]) * t / 1024);
  return result;
}
const channels = color => [parseInt(color.slice(1, 3), 16), parseInt(color.slice(3, 5), 16), parseInt(color.slice(5, 7), 16)];
const hex = values => "#" + values.map(v => Math.round(clamp(v, 0, 255)).toString(16).padStart(2, "0")).join("");
function mixColor(a, b, t) {
  const ac = channels(a), bc = channels(b);
  return hex(ac.map((v, i) => v + Math.trunc((bc[i] - v) * t / 1024)));
}
function sampleEffect(time) {
  const sequence = state.project.effects[trigger()];
  const total = sequence.steps.reduce((sum, step) => sum + step.duration, 0);
  if (!total || (!sequence.loop && time >= total)) return { color: "#000000", brightness: 0, vibration: 0, frequency: 0, volume: 0, wave: "sine", screen: false };
  let at = time % total, index = 0;
  while (index + 1 < sequence.steps.length && at >= sequence.steps[index].duration) at -= sequence.steps[index++].duration;
  const a = sequence.steps[index], b = sequence.steps[index + 1] || (sequence.loop ? sequence.steps[0] : a);
  let t = a.blend ? Math.floor(at * 1024 / a.duration) : 0;
  t = Math.floor(t * t * (3072 - 2 * t) / 1048576);
  const effect = { ...a, color: mixColor(a.color, b.color, t), screenColor: mixColor(a.screenColor, b.screenColor, t) };
  for (const key of ["brightness", "vibration", "frequency", "volume"]) effect[key] = a[key] + Math.trunc((b[key] - a[key]) * t / 1024);
  return effect;
}
function previewBindings() {
  const now = new Date();
  return {
    static: "", clock: now.toLocaleTimeString("en-GB", { hour12: false }),
    date: now.toLocaleDateString("en-GB", { weekday: "short", day: "2-digit", month: "short" }),
    alarm: state.scene === "alarm" ? "ALARM! 07:00" : "Alarm 07:00 ON",
    message: state.scene === "home" ? "Ready to chat\nConnect your phone." : `Message:\n${state.previewMessage}`,
    reply: "> I'm OK",
    hint: state.scene === "alarm" ? "Press BOOT to stop\nAlarm: up to 60s" : "Tap: choose / read\nDouble: send reply\nHold: set alarm",
    network: "192.168.1.42\nPair: 123456", microphone: `Mic: ${state.microphone}%`
  };
}
function tintedAtlas(font, color) {
  if (font.tints.has(color)) return font.tints.get(color);
  const tint = document.createElement("canvas");
  tint.width = font.image.width; tint.height = font.image.height;
  const context = tint.getContext("2d");
  context.drawImage(font.image, 0, 0);
  context.globalCompositeOperation = "source-in";
  context.fillStyle = color;
  context.fillRect(0, 0, tint.width, tint.height);
  if (font.tints.size >= 16) font.tints.clear();
  font.tints.set(color, tint);
  return tint;
}
function glyphFor(font, char) { return font.glyphs[char.codePointAt(0)] || font.glyphs[63] || font.glyphs[32]; }
function textWidth(font, text) { return [...text].reduce((sum, char) => sum + ((glyphFor(font, char).advance + 8) >> 4), 0); }
function wrapText(font, text, width) {
  const lines = [];
  for (const paragraph of text.replaceAll("\t", "  ").split("\n")) {
    let line = "";
    for (const word of paragraph.match(/\S+\s*|\s+/g) || [""]) {
      if (line && textWidth(font, line + word) > width) { lines.push(line.trimEnd()); line = ""; }
      for (const char of word) {
        if (line && textWidth(font, line + char) > width) { lines.push(line); line = ""; }
        line += char;
      }
    }
    lines.push(line.trimEnd());
  }
  return lines;
}
function drawText(layer, value, time) {
  const font = state.fonts.get(layer.font);
  if (!font?.image) {
    ctx.fillStyle = layer.color; ctx.font = "12px sans-serif"; ctx.textBaseline = "top";
    ctx.fillText(value.split("\n")[0], 0, 0, layer.width);
    return;
  }
  const lines = wrapText(font, value, layer.width), atlas = tintedAtlas(font, layer.color);
  let offset = 0;
  const overflow = lines.length * font.lineHeight - layer.height;
  if (layer.scroll && overflow > 0) {
    const travel = overflow / 0.025, phase = time % (travel * 2 + 2000);
    offset = phase < 1000 ? 0 : phase < 1000 + travel ? (phase - 1000) * .025 :
      phase < 2000 + travel ? overflow : Math.max(0, overflow - (phase - 2000 - travel) * .025);
  }
  lines.forEach((line, row) => {
    const width = textWidth(font, line);
    let x = layer.align === "center" ? Math.floor((layer.width - width) / 2) : layer.align === "right" ? layer.width - width : 0;
    for (const char of line) {
      const glyph = glyphFor(font, char);
      if (glyph.width && glyph.height) ctx.drawImage(atlas, glyph.x, glyph.y, glyph.width, glyph.height,
        x + glyph.ox, Math.round(row * font.lineHeight + font.lineHeight - font.baseline - glyph.height - glyph.oy - offset),
        glyph.width, glyph.height);
      x += (glyph.advance + 8) >> 4;
    }
  });
}
function imageFrame(image, time) {
  const total = image.durations.reduce((sum, value) => sum + value, 0);
  let at = total ? time % total : 0;
  for (let i = 0; i < image.frames.length; i++) {
    if (at < image.durations[i]) return image.frames[i];
    at -= image.durations[i];
  }
  return image.frames[0];
}
function draw() {
  if (!state.project) return;
  const effect = sampleEffect(state.time), bindings = previewBindings();
  ctx.globalAlpha = 1;
  ctx.fillStyle = effect.screen ? effect.screenColor : state.project.scenes[state.scene].background;
  ctx.fillRect(0, 0, W, H);
  for (const layer of sceneLayers()) {
    const position = pose(layer, state.time);
    ctx.save();
    ctx.translate(position.x, position.y);
    ctx.globalAlpha = position.opacity / 255;
    ctx.beginPath(); ctx.rect(0, 0, layer.width, layer.height); ctx.clip();
    if (layer.kind === "text") drawText(layer, layer.binding === "static" ? layer.text : bindings[layer.binding], state.time);
    else if (layer.kind === "image") {
      const image = state.images.get(layer.asset), frame = image?.frames?.length ? imageFrame(image, state.time) : null;
      if (frame) { ctx.imageSmoothingEnabled = true; ctx.drawImage(frame, 0, 0, layer.width, layer.height); ctx.imageSmoothingEnabled = false; }
      else { ctx.fillStyle = "#263c52"; ctx.fillRect(0, 0, layer.width, layer.height); }
    } else {
      ctx.fillStyle = layer.color;
      if (layer.kind === "ellipse") { ctx.beginPath(); ctx.ellipse(layer.width / 2, layer.height / 2, layer.width / 2, layer.height / 2, 0, 0, Math.PI * 2); ctx.fill(); }
      else ctx.fillRect(0, 0, layer.width, layer.height);
    }
    ctx.restore();
  }
  if ($("show-grid").checked) {
    ctx.strokeStyle = "#ffffff12"; ctx.lineWidth = .5;
    ctx.beginPath();
    for (let x = 0; x < W; x += 8) { ctx.moveTo(x, 0); ctx.lineTo(x, H); }
    for (let y = 0; y < H; y += 8) { ctx.moveTo(0, y); ctx.lineTo(W, y); }
    ctx.stroke();
  }
  const layer = selected();
  if (layer && layer.scene === state.scene && state.panel === "layout") {
    const position = pose(layer, state.time);
    ctx.globalAlpha = 1; ctx.strokeStyle = "#65a0ff"; ctx.lineWidth = 1;
    ctx.strokeRect(position.x - .5, position.y - .5, layer.width + 1, layer.height + 1);
    ctx.fillStyle = "#fff"; ctx.fillRect(position.x + layer.width - 2, position.y + layer.height - 2, 4, 4);
    ctx.strokeRect(position.x + layer.width - 2.5, position.y + layer.height - 2.5, 5, 5);
  }
  const led = hex(channels(effect.color).map(v => v * effect.brightness / 100));
  $("side-light").style.background = led;
  $("side-light").style.boxShadow = `-3px 0 14px ${led}80`;
  $("led-swatch").style.background = led;
  $("led-value").textContent = led.toUpperCase();
  $("vibe-meter").value = effect.vibration;
  $("vibe-value").textContent = `${effect.vibration}%`;
  $("sound-value").textContent = effect.frequency && effect.volume ? `${effect.frequency} Hz · ${effect.volume}%` : "Silent";
  $("time-label").textContent = `${(state.time / 1000).toFixed(2)} s`;
  $("scrubber").value = Math.round(state.time);
  if (state.audioOn && state.audio) {
    state.audio.oscillator.type = effect.wave;
    state.audio.oscillator.frequency.setTargetAtTime(Math.max(10, effect.frequency), state.audio.context.currentTime, .01);
    state.audio.gain.gain.setTargetAtTime(state.playing && effect.frequency ? effect.volume / 100 * .05 : 0, state.audio.context.currentTime, .015);
  }
}
function tick(now) {
  if (state.playing) state.time = (state.time + Math.min(100, now - state.lastTime)) % state.duration;
  state.lastTime = now;
  draw();
  requestAnimationFrame(tick);
}
function playing(value) {
  state.playing = value;
  $("play").textContent = value ? "Ⅱ" : "▶";
  $("play").setAttribute("aria-label", value ? "Pause preview" : "Play preview");
}

function newLayer(kind, asset = "") {
  if (state.project.layers.length >= 48) return toast("A project can contain up to 48 layers.", true);
  remember();
  const layer = {
    id: uid("layer"), name: kind === "text" ? "New text" : kind === "image" ? "Image" : "Shape",
    scene: state.scene, kind, binding: "static", text: kind === "text" ? "Hello there" : "",
    x: 16, y: 120, width: kind === "image" ? 96 : 140, height: kind === "text" ? 28 : 64,
    color: kind === "text" ? "#ffffff" : "#437aea", opacity: 255, font: "montserrat20",
    align: "left", scroll: false, asset, animation: { period: 2000, keys: [] }
  };
  state.project.layers.push(layer);
  state.selected = layer.id;
  state.panel = "layout";
  changed(); renderAll();
}
function deleteLayer() {
  if (!selected()) return;
  if (sceneLayers().length <= 1) return toast("Keep at least one layer in each scene.", true);
  remember();
  state.project.layers = state.project.layers.filter(layer => layer.id !== state.selected);
  state.selected = sceneLayers().at(-1)?.id;
  changed(); renderAll();
}
function duplicateLayer() {
  const layer = selected();
  if (!layer || state.project.layers.length >= 48) return;
  remember();
  const copy = structuredClone(layer);
  copy.id = uid("layer"); copy.name += " copy";
  shiftLayer(copy, clamp(copy.x + 4, -W, W), clamp(copy.y + 4, -H, H));
  state.project.layers.push(copy); state.selected = copy.id;
  changed(); renderAll();
}
function shiftLayer(layer, x, y) {
  const dx = x - layer.x, dy = y - layer.y;
  layer.x = x; layer.y = y;
  layer.animation.keys.forEach(key => { key.x = clamp(key.x + dx, -W, W); key.y = clamp(key.y + dy, -H, H); });
}
function reorderLayer(direction) {
  const indices = state.project.layers.map((l, i) => l.scene === state.scene ? i : -1).filter(i => i >= 0);
  const position = indices.findIndex(i => state.project.layers[i].id === state.selected);
  const target = position + direction;
  if (position < 0 || target < 0 || target >= indices.length) return;
  remember();
  const a = indices[position], b = indices[target];
  [state.project.layers[a], state.project.layers[b]] = [state.project.layers[b], state.project.layers[a]];
  changed(); renderLayers();
}
function motionPreset(name) {
  const layer = selected();
  if (!layer || !name) return;
  remember();
  const { x, y, opacity } = layer, period = layer.animation.period;
  const key = (at, dx = 0, dy = 0, alpha = opacity) => ({ at: Math.round(at), x: clamp(x + dx, -W, W), y: clamp(y + dy, -H, H), opacity: alpha });
  if (name === "none") layer.animation.keys = [];
  if (name === "fade") layer.animation.keys = [key(0, 0, 0, 0), key(period / 4), key(period * .75), key(period, 0, 0, 0)];
  if (name === "pulse") layer.animation.keys = [key(0), key(period / 2, 0, 0, 70), key(period)];
  if (name === "slideX") layer.animation.keys = [key(0, -12), key(period / 2, 12), key(period, -12)];
  if (name === "slideY") layer.animation.keys = [key(0, 0, -8), key(period / 2, 0, 8), key(period, 0, -8)];
  state.time = 0; changed(); renderInspector(); renderLayers();
}
function signalPreset(name) {
  if (!name) return;
  remember();
  const defaults = state.defaults.effects;
  let sequence = structuredClone(name === "alarm" ? defaults.alarm : name === "heartbeat" ? defaults.message : defaults.idle);
  if (name === "rainbow") sequence = { loop: true, steps: ["#ee5262", "#ffc95b", "#51ce9a", "#58b8ed", "#a68bea"].map(color => ({ ...defaults.idle.steps[0], duration: 450, color, blend: true })) };
  state.project.effects[trigger()] = sequence;
  state.time = 0; changed(); renderInspector();
}

function inspectorChange(event, continuous = false) {
  const target = event.target;
  if (target.id === "preview-message") { state.previewMessage = target.value; return; }
  if (target.id === "preview-mic") { state.microphone = Number(target.value); target.previousElementSibling.textContent = `Simulated microphone level · ${state.microphone}%`; return; }
  if (target.id === "motion-preset") { motionPreset(target.value); return; }
  if (target.id === "signal-preset") { signalPreset(target.value); return; }
  const editable = target.dataset.layer || target.dataset.scene || target.dataset.animation || target.dataset.key || target.dataset.step || target.dataset.hardware || target.id === "effect-loop";
  if (!editable) return;
  if (continuous) {
    if (state.continuous !== target) { remember(); state.continuous = target; }
  } else if (state.continuous === target) {
    state.continuous = null;
    return;
  } else remember();
  let value = target.type === "checkbox" ? target.checked : target.value;
  if (target.type === "number" || target.type === "range") {
    value = clamp(Math.round(Number(value) || 0), Number(target.min), Number(target.max));
    target.value = value;
  }
  let refresh = false;
  const layer = selected();
  if (target.dataset.layer && layer) {
    const key = target.dataset.layer;
    if (key === "kind" && value === "image" && !state.project.assets.some(a => a.kind === "image")) {
      state.undo.pop(); target.value = layer.kind;
      return toast("Import an image or GIF first with the Image button.", true);
    }
    if (key === "x") shiftLayer(layer, value, layer.y);
    else if (key === "y") shiftLayer(layer, layer.x, value);
    else layer[key] = value;
    if (key === "kind" && value === "image" && !layer.asset) layer.asset = state.project.assets.find(a => a.kind === "image").id;
    if (key === "opacity" && $("opacity-label")) $("opacity-label").textContent = `${Math.round(value / 255 * 100)}%`;
    refresh = ["kind", "binding", "font"].includes(key);
    if (key === "name" || key === "kind") renderLayers();
    if (key === "text" && !BUILTIN_FONTS[layer.font] && !continuous) loadAssets();
  } else if (target.dataset.scene) {
    state.project.scenes[state.scene][target.dataset.scene] = value;
  } else if (target.dataset.animation && layer) {
    const previous = layer.animation.period;
    layer.animation.period = value;
    layer.animation.keys.forEach(key => { key.at = Math.round(key.at * value / previous); });
    refresh = true;
  } else if (target.dataset.key && layer) {
    const index = Number(target.closest("[data-key-index]").dataset.keyIndex);
    const key = layer.animation.keys[index], property = target.dataset.key;
    if (property === "at" && layer.animation.keys.some((k, i) => i !== index && k.at === value)) {
      state.undo.pop(); target.value = key.at; return toast("Keyframe times must be unique.", true);
    }
    key[property] = value;
    layer.animation.keys.sort((a, b) => a.at - b.at);
    playing(false); state.time = key.at; refresh = property === "at";
  } else if (target.dataset.step) {
    const index = Number(target.closest("[data-step-index]").dataset.stepIndex);
    state.project.effects[trigger()].steps[index][target.dataset.step] = value;
    refresh = target.dataset.step === "screen";
  } else if (target.id === "effect-loop") {
    state.project.effects[trigger()].loop = target.checked;
  } else if (target.dataset.hardware) {
    const scope = target.dataset.scope;
    const object = scope.startsWith("button") ? state.project.hardware.buttons[Number(scope.slice(6))] : state.project.hardware[scope];
    object[target.dataset.hardware] = ["gpio", "bclk", "ws", "dout", "din"].includes(target.dataset.hardware) ? Number(value) : value;
    renderHardwareState();
  }
  if (target.type === "color" && target.nextElementSibling) target.nextElementSibling.textContent = value.toUpperCase();
  changed();
  if (refresh) renderInspector();
}

function pointerPosition(event) {
  const rect = canvas.getBoundingClientRect();
  return { x: (event.clientX - rect.left) * W / rect.width, y: (event.clientY - rect.top) * H / rect.height };
}
canvas.addEventListener("pointerdown", event => {
  if (!state.project || event.button !== 0) return;
  const point = pointerPosition(event), current = selected();
  let layer = current, resize = false;
  if (current) {
    const p = pose(current, state.time);
    resize = Math.abs(point.x - p.x - current.width) <= 6 && Math.abs(point.y - p.y - current.height) <= 6;
  }
  if (!resize) layer = [...sceneLayers()].reverse().find(l => {
    const p = pose(l, state.time);
    return point.x >= p.x && point.x <= p.x + l.width && point.y >= p.y && point.y <= p.y + l.height;
  });
  if (!layer) return;
  state.selected = layer.id; state.panel = "layout";
  playing(false);
  state.drag = { start: point, layer: structuredClone(layer), resize, recorded: false };
  canvas.setPointerCapture(event.pointerId);
  renderLayers(); renderInspector(); updateButtons();
});
canvas.addEventListener("pointermove", event => {
  if (!state.drag) return;
  const point = pointerPosition(event), drag = state.drag, layer = selected();
  const dx = point.x - drag.start.x, dy = point.y - drag.start.y;
  if (!drag.recorded && Math.abs(dx) + Math.abs(dy) < .7) return;
  if (!drag.recorded) { remember(); drag.recorded = true; }
  const snap = value => Math.round(value / ($("snap-grid").checked ? 4 : 1)) * ($("snap-grid").checked ? 4 : 1);
  if (drag.resize) {
    layer.width = clamp(snap(drag.layer.width + dx), 1, W);
    layer.height = clamp(snap(drag.layer.height + dy), 1, H);
  } else {
    layer.x = drag.layer.x; layer.y = drag.layer.y;
    layer.animation.keys = structuredClone(drag.layer.animation.keys);
    shiftLayer(layer, clamp(snap(drag.layer.x + dx), -W, W), clamp(snap(drag.layer.y + dy), -H, H));
  }
  changed();
});
function finishDrag(event) {
  if (!state.drag) return;
  state.drag = null;
  if (canvas.hasPointerCapture(event.pointerId)) canvas.releasePointerCapture(event.pointerId);
  renderInspector();
}
canvas.addEventListener("pointerup", finishDrag);
canvas.addEventListener("pointercancel", finishDrag);

function setScene(scene) {
  state.scene = scene; state.selected = sceneLayers()[0]?.id; state.time = 0;
  renderAll();
}
function setZoom(value) {
  state.zoom = value;
  canvas.style.width = `${W * value}px`; canvas.style.height = `${H * value}px`;
  $("zoom").value = String(value);
}
async function listen() {
  if (!state.audio) {
    const context = new AudioContext(), oscillator = context.createOscillator(), gain = context.createGain();
    gain.gain.value = 0; oscillator.connect(gain).connect(context.destination); oscillator.start();
    state.audio = { context, oscillator, gain };
  }
  state.audioOn = !state.audioOn;
  if (state.audioOn) await state.audio.context.resume();
  else state.audio.gain.gain.setTargetAtTime(0, state.audio.context.currentTime, .02);
  $("listen").textContent = state.audioOn ? "♫ Mute" : "♫ Listen";
  $("listen").classList.toggle("listening", state.audioOn);
}
function readDataURL(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(reader.result); reader.onerror = () => reject(new Error("Could not read the file."));
    reader.readAsDataURL(file);
  });
}
async function importAsset(file, kind) {
  if (!file) return;
  if (file.size > 2 * 1024 * 1024) throw new Error("Source images/GIFs/fonts must be no larger than 2 MB.");
  if (state.project.assets.length >= 8) throw new Error("A project supports up to eight imported assets.");
  const data = await readDataURL(file);
  const asset = { id: uid(kind), name: file.name, kind, data };
  if (kind === "font") { asset.size = 20; asset.characters = ""; }
  else {
    const image = await imageFromURL(data);
    const scale = Math.min(1, W / image.naturalWidth, H / image.naturalHeight, 96 / Math.max(image.naturalWidth, image.naturalHeight));
    asset.width = Math.max(1, Math.round(image.naturalWidth * scale));
    asset.height = Math.max(1, Math.round(image.naturalHeight * scale));
  }
  remember();
  state.project.assets.push(asset);
  changed(); renderAll();
  await loadAsset(asset);
  const cache = kind === "font" ? state.fonts.get(asset.id) : state.images.get(asset.id);
  if (cache?.failed) {
    state.project.assets = state.project.assets.filter(a => a.id !== asset.id);
    renderAll(); return;
  }
  if (kind === "image") {
    newLayer("image", asset.id);
    const layer = selected(); layer.width = asset.width; layer.height = asset.height;
    renderInspector();
  } else if (selected()?.kind === "text") {
    selected().font = asset.id; changed(); renderInspector();
  }
  toast(`${file.name} imported.`);
}
function editAsset(id) {
  const asset = state.project.assets.find(a => a.id === id);
  if (!asset) return;
  state.assetEditing = id;
  $("asset-heading").textContent = asset.name;
  $("asset-fields").innerHTML = field("Name", `<input id="asset-name" value="${h(asset.name)}" maxlength="120">`) +
    (asset.kind === "font" ?
      field("Font size · pixels", `<input id="asset-size" type="number" value="${asset.size}" min="8" max="48">`) +
      field("Extra characters", `<textarea id="asset-characters" maxlength="256">${h(asset.characters)}</textarea>`) +
      `<p class="help">ASCII, Latin-1 and your static text are included. Add other BMP characters here. Maximum 512 glyphs per font.</p>` :
      `<div class="field-row">${field("Decoded width", `<input id="asset-width" type="number" value="${asset.width}" min="1" max="172">`)}${field("Decoded height", `<input id="asset-height" type="number" value="${asset.height}" min="1" max="320">`)}</div>
      <p class="help">These dimensions control image quality and flash usage. Layer dimensions control its position and display size. Up to 120 frames; all decoded images share a 6 MB budget.</p>`);
  $("asset-dialog").showModal();
}
function renderPorts(ports) {
  if (JSON.stringify(ports) === JSON.stringify(state.portList)) return;
  const current = $("port").value; state.portList = ports;
  $("port").innerHTML = `<option value="">${ports.length ? "Choose a USB port" : "No ESP32 connected"}</option>` + options(ports.map(p => [p, p]), current);
  if (ports.includes(current)) $("port").value = current;
  else if (ports.length === 1) $("port").value = ports[0];
  $("device-dot").classList.toggle("connected", !!ports.length);
  $("device-status").textContent = ports.length ? "USB device available" : "Connect beeper by USB";
  updateButtons();
}
async function saveProject() {
  const revision = state.revision;
  state.pending = true; updateButtons();
  try {
    const result = await api("/api/save", { project: state.project });
    if (revision === state.revision) {
      state.project = result.project; state.dirty = false; $("dirty").textContent = "Saved locally";
    }
    toast("Project saved.");
  } finally { state.pending = false; updateButtons(); }
}
async function deploy(action) {
  state.pending = true; state.submittedRevision = state.revision; updateButtons();
  $("job-panel").hidden = false; $("job-phase").textContent = "Validating design…"; $("job-log").textContent = "";
  try {
    const status = await api(`/api/${action}`, { project: state.project, port: $("port").value });
    updateJob(status);
  } finally { state.pending = false; updateButtons(); }
}
function updateJob(job) {
  const wasBusy = state.busy;
  state.busy = job.busy;
  $("job-phase").textContent = job.phase || "Ready";
  $("job-log").textContent = job.log || "";
  $("job-indicator").className = "dot " + (job.busy ? "running" : job.result === 0 ? "connected" : job.result ? "failed" : "");
  if (job.report) $("job-budget").textContent = `${(job.report.storageBytes / 1024).toFixed(0)} KB images · ${job.report.layers} layers`;
  if (job.busy) { state.jobSeenBusy = true; $("job-panel").hidden = false; }
  if (wasBusy && !job.busy && state.jobSeenBusy) {
    if (job.result === 0) {
      if (state.revision === state.submittedRevision) { state.dirty = false; $("dirty").textContent = "Saved & built"; }
      toast(job.phase);
      $("status-text").textContent = state.dirty ? "Build finished. Your newer changes are still unsaved." : job.phase;
    } else toast(job.phase + ". See the build log.", true);
  }
  if (job.busy) $("job-log").scrollTop = $("job-log").scrollHeight;
  updateButtons();
}
async function poll() {
  try {
    const data = await api("/api/status"); updateJob(data.status); renderPorts(data.ports);
  } catch (error) {
    $("status-text").textContent = "Local editor server is unavailable. Restart it and reload this page.";
  } finally { setTimeout(poll, 1200); }
}

document.querySelectorAll("[data-scene]").forEach(button => button.addEventListener("click", () => setScene(button.dataset.scene)));
document.querySelectorAll("[data-panel]").forEach(button => button.addEventListener("click", () => { state.panel = button.dataset.panel; renderInspector(); }));
bind("layers", "click", event => {
  const row = event.target.closest("[data-layer-id]");
  if (row) { state.selected = row.dataset.layerId; state.panel = "layout"; renderLayers(); renderInspector(); updateButtons(); }
});
bind("layers", "keydown", event => { if (event.key === "Enter") event.target.click(); });
bind("assets", "click", event => {
  const insert = event.target.closest("[data-insert-asset]");
  if (insert) { newLayer("image", insert.dataset.insertAsset); return; }
  const row = event.target.closest("[data-asset]"); if (row) editAsset(row.dataset.asset);
});
bind("assets", "keydown", event => { if (event.key === "Enter") event.target.click(); });
bind("inspector", "change", event => inspectorChange(event));
bind("inspector", "input", event => {
  if (["range", "color"].includes(event.target.type) || event.target.tagName === "TEXTAREA") inspectorChange(event, true);
});
bind("inspector", "click", event => {
  const target = event.target;
  const layer = selected();
  if (target.id === "add-key" && layer) {
    if (layer.animation.keys.length >= 32) return toast("Up to 32 keyframes are supported.", true);
    remember();
    if (!layer.animation.keys.length) layer.animation.keys.push({ at: 0, x: layer.x, y: layer.y, opacity: layer.opacity });
    const at = Math.round(Math.min(state.time, layer.animation.period)), position = pose(layer, at);
    const existing = layer.animation.keys.find(key => key.at === at);
    if (existing) Object.assign(existing, position); else layer.animation.keys.push({ at, ...position });
    layer.animation.keys.sort((a, b) => a.at - b.at);
    changed(); renderInspector(); renderLayers();
  } else if (target.dataset.deleteKey !== undefined && layer) {
    remember(); layer.animation.keys.splice(Number(target.dataset.deleteKey), 1);
    changed(); renderInspector(); renderLayers();
  } else if (target.id === "add-step") {
    const steps = state.project.effects[trigger()].steps;
    if (steps.length >= 32) return toast("Up to 32 steps are supported.", true);
    remember(); steps.push(structuredClone(steps.at(-1))); changed(); renderInspector();
  } else if (target.dataset.deleteStep !== undefined) {
    remember(); state.project.effects[trigger()].steps.splice(Number(target.dataset.deleteStep), 1); changed(); renderInspector();
  } else if (target.dataset.moveStep !== undefined) {
    remember();
    const steps = state.project.effects[trigger()].steps, from = Number(target.dataset.moveStep), to = from + Number(target.dataset.direction);
    [steps[from], steps[to]] = [steps[to], steps[from]]; changed(); renderInspector();
  }
});
bind("project-name", "change", event => { remember(); state.project.name = event.target.value; changed(); });
bind("add-text", "click", () => newLayer("text"));
bind("add-box", "click", () => newLayer("box"));
bind("add-image", "click", () => $("image-file").click());
bind("import-font", "click", () => $("font-file").click());
bind("image-file", "change", async event => { const file = event.target.files[0]; event.target.value = ""; await importAsset(file, "image"); });
bind("font-file", "change", async event => { const file = event.target.files[0]; event.target.value = ""; await importAsset(file, "font"); });
bind("delete-layer", "click", deleteLayer);
bind("duplicate-layer", "click", duplicateLayer);
bind("layer-up", "click", () => reorderLayer(1));
bind("layer-down", "click", () => reorderLayer(-1));
bind("undo", "click", () => undo());
bind("redo", "click", () => undo(true));
bind("play", "click", () => playing(!state.playing));
bind("restart", "click", () => { state.time = 0; playing(true); });
bind("scrubber", "input", event => { playing(false); state.time = Number(event.target.value); });
bind("zoom", "change", event => setZoom(Number(event.target.value)));
bind("listen", "click", listen);
bind("port", "change", updateButtons);
bind("save-project", "click", saveProject);
bind("build-project", "click", () => deploy("build"));
bind("flash-project", "click", () => deploy("flash"));
bind("close-job", "click", () => { $("job-panel").hidden = true; });
bind("import-project", "click", () => $("project-file").click());
bind("project-file", "change", async event => {
  const file = event.target.files[0]; event.target.value = "";
  if (!file) return;
  if (file.size > 16 * 1024 * 1024) throw new Error("Project files must be no larger than 16 MB.");
  const data = await api("/api/validate", { project: JSON.parse(await file.text()) });
  remember(); state.project = data.project; state.selected = sceneLayers()[0]?.id;
  changed(); renderAll(); loadAssets(); toast("Project opened. Save it to make it the active design.");
});
bind("export-project", "click", () => {
  const blob = new Blob([JSON.stringify(state.project, null, 2)], { type: "application/json" });
  const link = document.createElement("a"); link.href = URL.createObjectURL(blob);
  link.download = (state.project.name.replace(/[^a-z0-9_-]/gi, "_") || "beeper") + ".json";
  link.click(); setTimeout(() => URL.revokeObjectURL(link.href), 1000);
});
bind("reset-project", "click", () => {
  remember(); state.project = structuredClone(state.defaults); state.selected = sceneLayers()[0].id;
  changed(); renderAll(); toast("Starter design restored. Undo returns to your previous design.");
});
bind("close-asset", "click", () => $("asset-dialog").close());
bind("asset-form", "submit", async event => {
  event.preventDefault();
  const asset = state.project.assets.find(a => a.id === state.assetEditing);
  remember(); asset.name = $("asset-name").value;
  if (asset.kind === "font") { asset.size = clamp(Number($("asset-size").value), 8, 48); asset.characters = $("asset-characters").value; }
  else { asset.width = clamp(Number($("asset-width").value), 1, W); asset.height = clamp(Number($("asset-height").value), 1, H); }
  $("asset-dialog").close(); changed(); renderAll(); await loadAsset(asset);
});
bind("delete-asset", "click", () => {
  const id = state.assetEditing;
  if (state.project.layers.some(layer => layer.asset === id || layer.font === id)) return toast("This asset is used by a layer. Replace or remove that layer first.", true);
  remember(); state.project.assets = state.project.assets.filter(a => a.id !== id);
  $("asset-dialog").close(); changed(); renderAll();
});
document.addEventListener("keydown", event => {
  if (!state.project || event.target.matches("input,textarea,select") || $("asset-dialog").open) return;
  if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === "z") { event.preventDefault(); undo(event.shiftKey); }
  else if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === "s") { event.preventDefault(); if (!state.busy) saveProject().catch(handleError); }
  else if (event.key === "Delete" || event.key === "Backspace") { event.preventDefault(); deleteLayer(); }
  else if (event.key === " ") { event.preventDefault(); playing(!state.playing); }
});
document.addEventListener("visibilitychange", () => { if (document.hidden && state.audioOn) listen().catch(handleError); });
window.addEventListener("beforeunload", event => { if (state.dirty) { event.preventDefault(); event.returnValue = ""; } });

async function initialize() {
  updateButtons();
  const data = await api("/api/bootstrap");
  state.token = data.token; state.project = data.project; state.defaults = data.defaults;
  state.pins = data.pins; state.selected = sceneLayers()[0].id;
  $("project-name").disabled = false; $("dirty").textContent = "Saved locally";
  $("status-text").textContent = "Choose a screen, move its layers, then build and flash over USB.";
  const zoom = window.innerHeight < 800 ? 1.25 : window.innerHeight < 920 ? 1.5 : 1.75;
  setZoom(zoom);
  renderAll(); renderPorts(data.ports);
  if (data.status) updateJob(data.status);
  await Promise.all(Object.keys(BUILTIN_FONTS).map(name => loadFont(name).catch(handleError)));
  loadAssets(); poll();
}
initialize().catch(handleError);
requestAnimationFrame(tick);
