// library/runtime/engine-core.js
function utf8Encode(text) {
  const bytes = [];
  for (const char of text) {
    const code = char.codePointAt(0);
    if (code < 128)
      bytes.push(code);
    else if (code < 2048)
      bytes.push(192 | code >> 6, 128 | code & 63);
    else if (code < 65536)
      bytes.push(224 | code >> 12, 128 | code >> 6 & 63, 128 | code & 63);
    else
      bytes.push(240 | code >> 18, 128 | code >> 12 & 63, 128 | code >> 6 & 63, 128 | code & 63);
  }
  return Uint8Array.from(bytes);
}
function utf8Decode(bytes) {
  let text = "";
  for (let i = 0;i < bytes.length; ) {
    const b = bytes[i];
    const length = b < 128 ? 1 : b < 224 ? 2 : b < 240 ? 3 : 4;
    let code = length === 1 ? b : b & 255 >> length + 1;
    for (let k = 1;k < length; k++)
      code = code << 6 | bytes[i + k] & 63;
    text += String.fromCodePoint(code);
    i += length;
  }
  return text;
}
var IMPORTS = {
  env: { emscripten_notify_memory_growth: () => {} },
  wasi_snapshot_preview1: { fd_close: () => 0, fd_write: () => 0, fd_seek: () => 0, proc_exit: () => {} }
};

class EngineCore {
  static create(module, { sampleRate: sampleRate2 = 48000, maxBlock = 128 } = {}) {
    const compiled = module instanceof WebAssembly.Module ? module : new WebAssembly.Module(module);
    return new EngineCore(new WebAssembly.Instance(compiled, IMPORTS), sampleRate2, maxBlock);
  }
  constructor(instance, sampleRate2, maxBlock) {
    this.wasm = instance.exports;
    this.wasm._initialize?.();
    this.sampleRate = sampleRate2;
    this.maxBlock = maxBlock;
    this.engine = this.wasm.lemnos_create();
    this.wasm.lemnos_prepare(this.engine, sampleRate2, maxBlock);
    this.leftPtr = this.wasm.malloc(maxBlock * 4);
    this.rightPtr = this.wasm.malloc(maxBlock * 4);
    this.scratchPtr = this.wasm.malloc(4096 * 4);
    this.params = Array.from({ length: this.wasm.lemnos_param_count() }, (_, index) => ({
      index,
      address: this.readString(this.wasm.lemnos_param_address(index)),
      min: this.wasm.lemnos_param_min(index),
      max: this.wasm.lemnos_param_max(index),
      default: this.wasm.lemnos_param_default(index)
    }));
    this.indexOf = new Map(this.params.map((param) => [param.address, param.index]));
    this.players = Array.from({ length: this.wasm.lemnos_motion_count() }, (_, index) => this.readString(this.wasm.lemnos_motion_address(index)));
    this.watchAddresses = Array.from({ length: this.wasm.lemnos_watch_count() }, (_, index) => this.readString(this.wasm.lemnos_watch_address(index)));
    this.watchPtr = this.wasm.malloc(Math.max(1, this.watchAddresses.length) * 4);
    this.spectrumAddresses = Array.from({ length: this.wasm.lemnos_spectrum_count() }, (_, index) => this.readString(this.wasm.lemnos_spectrum_address(index)));
  }
  get heapF32() {
    return this.view("f32", Float32Array);
  }
  get heapU8() {
    return this.view("u8", Uint8Array);
  }
  view(name, Type) {
    const buffer = this.wasm.memory.buffer;
    if (this[`_${name}`]?.buffer !== buffer)
      this[`_${name}`] = new Type(buffer);
    return this[`_${name}`];
  }
  readString(ptr) {
    const bytes = this.heapU8;
    let end = ptr;
    while (bytes[end])
      end++;
    return utf8Decode(bytes.subarray(ptr, end));
  }
  paramIndex(param) {
    if (typeof param === "number")
      return param >= 0 && param < this.params.length ? param : -1;
    return this.indexOf.get(param) ?? -1;
  }
  setParam(param, value) {
    const index = this.paramIndex(param);
    if (index >= 0)
      this.wasm.lemnos_set_param(this.engine, index, Number(value));
    return index;
  }
  getParam(param) {
    const index = this.paramIndex(param);
    return index >= 0 ? this.wasm.lemnos_get_param(this.engine, index) : NaN;
  }
  noteOn(note, velocity = 1) {
    this.wasm.lemnos_note_on(this.engine, note | 0, velocity);
  }
  noteOff(note) {
    this.wasm.lemnos_note_off(this.engine, note | 0);
  }
  allNotesOff() {
    this.wasm.lemnos_all_notes_off(this.engine);
  }
  pitchBend(semitones) {
    this.wasm.lemnos_pitch_bend(this.engine, semitones);
  }
  midi(bytes) {
    const n = Math.min(3, bytes.length), heap = this.heapU8;
    for (let i = 0;i < n; i++)
      heap[this.scratchPtr + i] = bytes[i];
    this.wasm.lemnos_midi(this.engine, this.scratchPtr, n);
  }
  wheel(travel) {
    this.wasm.lemnos_wheel(this.engine, travel);
  }
  process(left, right) {
    for (let done = 0;done < left.length; ) {
      const n = Math.min(this.maxBlock, left.length - done);
      this.wasm.lemnos_process(this.engine, this.leftPtr, this.rightPtr, n);
      const heap = this.heapF32;
      left.set(heap.subarray(this.leftPtr >> 2, (this.leftPtr >> 2) + n), done);
      right.set(heap.subarray(this.rightPtr >> 2, (this.rightPtr >> 2) + n), done);
      done += n;
    }
  }
  scope(n = 1024) {
    const count = Math.max(0, Math.min(4096, n | 0));
    this.wasm.lemnos_scope(this.engine, this.scratchPtr, count);
    return this.heapF32.slice(this.scratchPtr >> 2, (this.scratchPtr >> 2) + count);
  }
  spectrum(n = 2048) {
    const count = Math.max(0, Math.min(4096, n | 0));
    return this.spectrumAddresses.map((_, index) => {
      this.wasm.lemnos_spectrum(this.engine, index, this.scratchPtr, count);
      return this.heapF32.slice(this.scratchPtr >> 2, (this.scratchPtr >> 2) + count);
    });
  }
  setMotion(player, slot, xy) {
    const index = typeof player === "number" ? player : this.players.indexOf(player);
    if (index < 0 || index >= this.players.length)
      return -1;
    const count = Math.min(1000, Math.floor((xy?.length || 0) / 2)), heap = this.heapF32, at = this.scratchPtr >> 2;
    for (let i = 0;i < 2 * count; i++)
      heap[at + i] = Number(xy[i]) || 0;
    this.wasm.lemnos_set_motion(this.engine, index, slot | 0, this.scratchPtr, count);
    return index;
  }
  watch() {
    const count = this.watchAddresses.length;
    if (!count)
      return new Float32Array(0);
    this.wasm.lemnos_watch(this.engine, this.watchPtr, count);
    return this.heapF32.slice(this.watchPtr >> 2, (this.watchPtr >> 2) + count);
  }
  getState() {
    const length = this.wasm.lemnos_get_state(this.engine, 0, 0), ptr = this.wasm.malloc(length + 1);
    this.wasm.lemnos_get_state(this.engine, ptr, length + 1);
    const json = this.readString(ptr);
    this.wasm.free(ptr);
    return json;
  }
  setState(state) {
    const bytes = utf8Encode(typeof state === "string" ? state : JSON.stringify(state)), ptr = this.wasm.malloc(bytes.length + 1);
    this.heapU8.set(bytes, ptr);
    this.heapU8[ptr + bytes.length] = 0;
    const set = this.wasm.lemnos_set_state(this.engine, ptr);
    this.wasm.free(ptr);
    return set;
  }
  destroy() {
    for (const ptr of [this.leftPtr, this.rightPtr, this.scratchPtr, this.watchPtr])
      this.wasm.free(ptr);
    this.wasm.lemnos_destroy(this.engine);
    this.engine = 0;
  }
}

// library/runtime/worklet.js
class LemnosEngineProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const { module, maxBlock = 128 } = options?.processorOptions || {};
    this.left = new Float32Array(maxBlock);
    this.right = new Float32Array(maxBlock);
    try {
      this.core = EngineCore.create(module, { sampleRate, maxBlock });
      this.port.postMessage({ type: "ready", params: this.core.params, players: this.core.players, watch: this.core.watchAddresses, spectra: this.core.spectrumAddresses });
    } catch (error) {
      this.core = null;
      this.port.postMessage({ type: "error", message: String(error?.message || error) });
    }
    this.port.onmessage = (event) => this.receive(event.data);
  }
  receive(message) {
    const core = this.core;
    if (!core || !message)
      return;
    switch (message.type) {
      case "param":
        core.setParam(message.param, message.value);
        break;
      case "noteOn":
        core.noteOn(message.note, message.velocity ?? 1);
        break;
      case "noteOff":
        core.noteOff(message.note);
        break;
      case "allNotesOff":
        core.allNotesOff();
        break;
      case "bend":
        core.pitchBend(message.semitones);
        break;
      case "midi":
        core.midi(message.bytes);
        break;
      case "wheel":
        core.wheel(message.travel);
        break;
      case "motion":
        core.setMotion(message.player, message.slot, message.xy);
        break;
      case "getState":
        this.port.postMessage({ type: "reply", id: message.id, value: core.getState() });
        break;
      case "setState":
        this.port.postMessage({ type: "reply", id: message.id, value: core.setState(message.state) });
        break;
      case "scope":
        this.port.postMessage({ type: "reply", id: message.id, value: core.scope(message.n) });
        break;
      case "watch":
        this.port.postMessage({ type: "reply", id: message.id, value: core.watch() });
        break;
      case "spectrum":
        this.port.postMessage({ type: "reply", id: message.id, value: core.spectrum(message.n) });
        break;
      default:
        break;
    }
  }
  process(_inputs, outputs) {
    const out = outputs[0];
    if (!this.core || !out?.length)
      return true;
    const n = out[0].length;
    if (this.left.length < n) {
      this.left = new Float32Array(n);
      this.right = new Float32Array(n);
    }
    const left = this.left.subarray(0, n), right = this.right.subarray(0, n);
    this.core.process(left, right);
    if (out.length === 1)
      for (let i = 0;i < n; i++)
        out[0][i] = 0.5 * (left[i] + right[i]);
    else {
      out[0].set(left);
      out[1].set(right);
    }
    return true;
  }
}
registerProcessor("lemnos-engine", LemnosEngineProcessor);
