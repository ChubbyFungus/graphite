class GraphiteApp {
  constructor() {
    this.container = document.getElementById('canvas-container');
    this.paperCanvas = document.getElementById('paper-canvas');
    this.drawingCanvas = document.getElementById('drawing-canvas');
    this.paperCtx = this.paperCanvas.getContext('2d');
    this.ctx = this.drawingCanvas.getContext('2d');
    this.isDrawing = false;
    this.strokeWarmup = 0;
    this.lastPoint = null;
    this.prevPoint = null;
    this.currentTool = 'pencil';
    this.currentPressure = 0;
    this.canvasSizeMode = 'auto';
    this.settings = { grade: 4, size: 8, opacity: 100, paper: 'medium', usePressure: true };
    this.grades = {
      0: { name: '4H', hardness: 0.3, opacity: 0.15, texture: 0.9 },
      1: { name: '3H', hardness: 0.35, opacity: 0.2, texture: 0.85 },
      2: { name: '2H', hardness: 0.4, opacity: 0.25, texture: 0.8 },
      3: { name: 'H', hardness: 0.5, opacity: 0.3, texture: 0.75 },
      4: { name: 'HB', hardness: 0.6, opacity: 0.4, texture: 0.65 },
      5: { name: 'B', hardness: 0.7, opacity: 0.5, texture: 0.55 },
      6: { name: '2B', hardness: 0.8, opacity: 0.6, texture: 0.45 },
      7: { name: '4B', hardness: 0.85, opacity: 0.7, texture: 0.35 },
      8: { name: '6B', hardness: 0.9, opacity: 0.8, texture: 0.25 },
      9: { name: '8B', hardness: 0.95, opacity: 0.9, texture: 0.15 }
    };
    this.paperTextures = {
      smooth: { grain: 0.02, roughness: 0.1 },
      medium: { grain: 0.06, roughness: 0.3 },
      rough: { grain: 0.12, roughness: 0.5 },
      toned: { grain: 0.04, roughness: 0.2, tint: '#b8b8b8' }
    };
    this.undoStack = [];
    this.redoStack = [];
    this.maxHistory = 50;
    this.grainData = null;
    this.smudgeBuffer = null;
    this.initCanvas();
    this.initControls();
    this.initEvents();
    this.generatePaper();
  }
  initCanvas() {
    const resize = () => { if (this.canvasSizeMode !== 'auto') return; this.resizeToFit(); };
    this.resizeToFit();
    window.addEventListener('resize', resize);
  }
  resizeToFit() {
    const rect = this.container.parentElement.getBoundingClientRect();
    const padding = 40, maxWidth = rect.width - padding, maxHeight = rect.height - padding, ratio = 1.414;
    let width, height;
    if (maxWidth / maxHeight > 1 / ratio) { height = maxHeight; width = height / ratio; }
    else { width = maxWidth; height = width * ratio; }
    this.setCanvasSize(Math.floor(width), Math.floor(height));
  }
  setCanvasSize(width, height) {
    const imageData = this.ctx.getImageData(0, 0, this.drawingCanvas.width, this.drawingCanvas.height);
    this.paperCanvas.width = width; this.paperCanvas.height = height;
    this.drawingCanvas.width = width; this.drawingCanvas.height = height;
    this.container.style.width = width + 'px'; this.container.style.height = height + 'px';
    this.ctx.putImageData(imageData, 0, 0);
    this.generatePaper(); this.updateStats();
  }
  generatePaper() {
    const { width, height } = this.paperCanvas;
    if (width === 0 || height === 0) return;
    const texture = this.paperTextures[this.settings.paper];
    const baseColor = texture.tint || '#f5f5f0';
    this.paperCtx.fillStyle = baseColor;
    this.paperCtx.fillRect(0, 0, width, height);
    const imageData = this.paperCtx.getImageData(0, 0, width, height);
    const data = imageData.data;
    this.grainData = new Float32Array(width * height);
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        const i = (y * width + x) * 4, gi = y * width + x;
        let noise = this.noise(x * 0.1, y * 0.1) * 0.5 + this.noise(x * 0.3, y * 0.3) * 0.3 + this.noise(x * 0.7, y * 0.7) * 0.2 + (Math.random() - 0.5) * texture.grain * 2;
        this.grainData[gi] = 0.5 + noise * texture.roughness;
        const variation = noise * 20 * texture.roughness;
        data[i] = Math.max(0, Math.min(255, data[i] + variation));
        data[i + 1] = Math.max(0, Math.min(255, data[i + 1] + variation));
        data[i + 2] = Math.max(0, Math.min(255, data[i + 2] + variation));
      }
    }
    this.paperCtx.putImageData(imageData, 0, 0);
  }
  noise(x, y) { const n = Math.sin(x * 12.9898 + y * 78.233) * 43758.5453; return n - Math.floor(n) - 0.5; }
  getGrain(x, y) {
    if (!this.grainData) return 0.5;
    const { width, height } = this.paperCanvas;
    const ix = Math.floor(Math.max(0, Math.min(width - 1, x)));
    const iy = Math.floor(Math.max(0, Math.min(height - 1, y)));
    return this.grainData[iy * width + ix];
  }
  initControls() {
    document.querySelectorAll('.tool-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        document.querySelectorAll('.tool-btn').forEach(b => b.classList.remove('active'));
        btn.classList.add('active'); this.currentTool = btn.dataset.tool; this.updateCursor();
      });
    });
    const gradeSlider = document.getElementById('grade-slider');
    const gradeDisplay = document.getElementById('grade-display');
    gradeSlider.addEventListener('input', () => { this.settings.grade = parseInt(gradeSlider.value); gradeDisplay.textContent = this.grades[this.settings.grade].name; });
    const sizeSlider = document.getElementById('size-slider');
    const sizeDisplay = document.getElementById('size-display');
    sizeSlider.addEventListener('input', () => { this.settings.size = parseInt(sizeSlider.value); sizeDisplay.textContent = this.settings.size + 'px'; });
    const opacitySlider = document.getElementById('opacity-slider');
    const opacityDisplay = document.getElementById('opacity-display');
    opacitySlider.addEventListener('input', () => { this.settings.opacity = parseInt(opacitySlider.value); opacityDisplay.textContent = this.settings.opacity + '%'; });
    const paperSelect = document.getElementById('paper-select');
    paperSelect.addEventListener('change', () => { this.settings.paper = paperSelect.value; this.generatePaper(); });
    const canvasSizeSelect = document.getElementById('canvas-size-select');
    canvasSizeSelect.addEventListener('change', () => {
      const val = canvasSizeSelect.value;
      if (val === 'auto') { this.canvasSizeMode = 'auto'; this.resizeToFit(); }
      else { this.canvasSizeMode = 'fixed'; const [w, h] = val.split('x').map(Number); this.setCanvasSize(w, h); }
    });
    const pressureToggle = document.getElementById('pressure-toggle');
    pressureToggle.addEventListener('change', () => { this.settings.usePressure = pressureToggle.checked; });
    document.getElementById('undo-btn').addEventListener('click', () => this.undo());
    document.getElementById('redo-btn').addEventListener('click', () => this.redo());
    document.getElementById('clear-btn').addEventListener('click', () => this.clear());
    document.getElementById('save-btn').addEventListener('click', () => this.save());
    document.addEventListener('keydown', (e) => {
      if (e.ctrlKey || e.metaKey) {
        if (e.key === 'z') { e.preventDefault(); this.undo(); }
        if (e.key === 'y') { e.preventDefault(); this.redo(); }
        if (e.key === 's') { e.preventDefault(); this.save(); }
      }
      if (e.key === 'p') this.selectTool('pencil');
      if (e.key === 'e') this.selectTool('eraser-hard');
      if (e.key === 'k') this.selectTool('eraser-kneaded');
      if (e.key === 'f') this.selectTool('fan-brush');
      if (e.key === 'b') this.selectTool('powder-brush');
      if (e.key === 't') this.selectTool('tortillon');
      if (e.key === '[') { this.settings.size = Math.max(1, this.settings.size - 2); document.getElementById('size-slider').value = this.settings.size; document.getElementById('size-display').textContent = this.settings.size + 'px'; }
      if (e.key === ']') { this.settings.size = Math.min(100, this.settings.size + 2); document.getElementById('size-slider').value = this.settings.size; document.getElementById('size-display').textContent = this.settings.size + 'px'; }
    });
  }
  selectTool(tool) { document.querySelectorAll('.tool-btn').forEach(btn => { btn.classList.toggle('active', btn.dataset.tool === tool); }); this.currentTool = tool; this.updateCursor(); }
  updateCursor() {
    this.drawingCanvas.className = '';
    if (this.currentTool === 'pencil') this.drawingCanvas.classList.add('cursor-pencil');
    else if (this.currentTool.includes('eraser')) this.drawingCanvas.classList.add('cursor-eraser');
    else this.drawingCanvas.classList.add('cursor-smudge');
  }
  initEvents() {
    const canvas = this.drawingCanvas;
    canvas.addEventListener('pointerdown', (e) => this.onPointerDown(e));
    canvas.addEventListener('pointermove', (e) => this.onPointerMove(e));
    canvas.addEventListener('pointerup', (e) => this.onPointerUp(e));
    canvas.addEventListener('pointerleave', (e) => this.onPointerUp(e));
    canvas.addEventListener('contextmenu', (e) => e.preventDefault());
  }
  getPoint(e) {
    const rect = this.drawingCanvas.getBoundingClientRect();
    return { x: (e.clientX - rect.left) * (this.drawingCanvas.width / rect.width), y: (e.clientY - rect.top) * (this.drawingCanvas.height / rect.height), pressure: e.pointerType === 'mouse' ? 0 : e.pressure, tiltX: e.tiltX || 0, tiltY: e.tiltY || 0 };
  }
  onPointerDown(e) {
    if (e.button !== 0) return;
    this.isDrawing = true; this.strokeWarmup = 3;
    this.drawingCanvas.setPointerCapture(e.pointerId);
    this.saveState();
    const point = this.getPoint(e);
    this.lastPoint = point; this.prevPoint = point;
    this.currentPressure = point.pressure;
    this.updatePressureIndicator(point.pressure);
  }
  onPointerMove(e) {
    const point = this.getPoint(e);
    this.currentPressure = point.pressure;
    this.updatePressureIndicator(point.pressure);
    if (!this.isDrawing) return;
    if (this.strokeWarmup > 0) { this.strokeWarmup--; this.lastPoint = point; return; }
    this.drawStroke(this.lastPoint, point);
    this.prevPoint = this.lastPoint; this.lastPoint = point;
  }
  onPointerUp(e) {
    if (!this.isDrawing) return;
    this.isDrawing = false; this.strokeWarmup = 0;
    this.drawingCanvas.releasePointerCapture(e.pointerId);
    this.lastPoint = null; this.prevPoint = null; this.smudgeBuffer = null;
  }
  updatePressureIndicator(pressure) { document.getElementById('pressure-bar').style.width = (pressure * 100) + '%'; }
  drawStroke(from, to) {
    const dist = Math.hypot(to.x - from.x, to.y - from.y);
    if (dist < 1.5) return;
    if (to.pressure < 0.01) return;
    const baseSpacing = Math.max(1, this.settings.size * 0.1);
    const steps = Math.max(1, Math.ceil(dist / baseSpacing));
    for (let i = 0; i <= steps; i++) {
      const t = steps === 0 ? 0 : i / steps;
      const x = from.x + (to.x - from.x) * t;
      const y = from.y + (to.y - from.y) * t;
      const pressure = from.pressure + (to.pressure - from.pressure) * t;
      this.drawBrushStamp(x, y, pressure, from, to);
    }
  }
  drawBrushStamp(x, y, pressure, from, to) {
    const grade = this.grades[this.settings.grade];
    const grain = this.getGrain(x, y);
    switch (this.currentTool) {
      case 'pencil': this.drawPencilStamp(x, y, pressure, grade, grain); break;
      case 'eraser-hard': this.drawHardEraser(x, y, pressure); break;
      case 'eraser-kneaded': this.drawKneadedEraser(x, y, pressure, grain); break;
      case 'fan-brush': this.drawFanBrush(x, y, pressure, from, to); break;
      case 'powder-brush': this.drawPowderBrush(x, y, pressure, from, to); break;
      case 'tortillon': this.drawTortillon(x, y, pressure, from, to); break;
    }
  }
  drawPencilStamp(x, y, pressure, grade, grain) {
    const ctx = this.ctx;
    const baseSize = this.settings.size;
    const size = baseSize * (0.3 + pressure * 0.7);
    const grainDeposit = 1 - (grain * grade.texture);
    const baseOpacity = grade.opacity * (this.settings.opacity / 100);
    const opacity = baseOpacity * (0.3 + pressure * 0.7) * grainDeposit;
    const numStrokes = Math.ceil(3 + pressure * 4);
    for (let i = 0; i < numStrokes; i++) {
      const offsetX = (Math.random() - 0.5) * size * 0.4;
      const offsetY = (Math.random() - 0.5) * size * 0.4;
      const strokeSize = size * (0.5 + Math.random() * 0.5);
      const strokeOpacity = opacity * (0.3 + Math.random() * 0.7);
      ctx.save();
      ctx.globalAlpha = strokeOpacity;
      ctx.globalCompositeOperation = 'multiply';
      const grayValue = 25 + (1 - grade.hardness) * 20;
      ctx.fillStyle = `rgb(${grayValue}, ${grayValue}, ${grayValue + 5})`;
      ctx.beginPath();
      ctx.ellipse(x + offsetX, y + offsetY, strokeSize / 2, strokeSize / 2 * (0.8 + Math.random() * 0.2), Math.random() * Math.PI, 0, Math.PI * 2);
      ctx.fill();
      ctx.restore();
    }
  }
  drawHardEraser(x, y, pressure) {
    const ctx = this.ctx;
    const size = this.settings.size * 2 * (0.5 + pressure * 0.5);
    ctx.save();
    ctx.globalCompositeOperation = 'destination-out';
    ctx.globalAlpha = 0.8 + pressure * 0.2;
    ctx.beginPath();
    ctx.arc(x, y, size / 2, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
  }
  drawKneadedEraser(x, y, pressure, grain) {
    const ctx = this.ctx;
    const size = this.settings.size * 2 * (0.5 + pressure * 0.5);
    const radius = size / 2;
    const sx = Math.max(0, Math.floor(x - radius));
    const sy = Math.max(0, Math.floor(y - radius));
    const sw = Math.min(this.drawingCanvas.width - sx, Math.ceil(size));
    const sh = Math.min(this.drawingCanvas.height - sy, Math.ceil(size));
    if (sw <= 0 || sh <= 0) return;
    const imageData = ctx.getImageData(sx, sy, sw, sh);
    const data = imageData.data;
    const baseLift = 0.01 + pressure * 0.03;
    for (let py = 0; py < sh; py++) {
      for (let px = 0; px < sw; px++) {
        const i = (py * sw + px) * 4;
        const origAlpha = data[i + 3];
        if (origAlpha < 3) continue;
        const dx = px - sw/2, dy = py - sh/2;
        const dist = Math.sqrt(dx*dx + dy*dy) / radius;
        if (dist > 1) continue;
        const falloff = Math.pow(1 - dist, 2);
        const localGrain = this.getGrain(sx + px, sy + py);
        const grainMod = 0.3 + localGrain * 0.7;
        const patchiness = 0.3 + Math.random() * 0.7;
        const lift = baseLift * falloff * grainMod * patchiness;
        data[i + 3] = Math.max(0, origAlpha - origAlpha * lift);
      }
    }
    ctx.putImageData(imageData, sx, sy);
  }
  // Fan brush: wide feathery blur - ONLY smooths existing graphite, never adds
  drawFanBrush(x, y, pressure, from, to) {
    const ctx = this.ctx;
    const width = Math.max(40, this.settings.size * 6);
    const height = Math.max(15, this.settings.size * 1.5);
    const strength = 0.02 + pressure * 0.04;
    const sx = Math.max(0, Math.floor(x - width/2));
    const sy = Math.max(0, Math.floor(y - height/2));
    const sw = Math.min(this.drawingCanvas.width - sx, Math.ceil(width));
    const sh = Math.min(this.drawingCanvas.height - sy, Math.ceil(height));
    if (sw <= 0 || sh <= 0) return;
    const imageData = ctx.getImageData(sx, sy, sw, sh);
    const data = imageData.data;
    const origData = new Uint8ClampedArray(data);
    const numStrands = 8, strandSpacing = sw / numStrands;
    for (let py = 0; py < sh; py++) {
      for (let px = 0; px < sw; px++) {
        const i = (py * sw + px) * 4;
        const origAlpha = origData[i + 3];
        if (origAlpha < 10) continue;
        const cx = (px - sw/2) / (sw/2), cy = (py - sh/2) / (sh/2);
        const d = Math.sqrt(cx*cx + cy*cy*4);
        if (d > 1) continue;
        const strandPos = (px % strandSpacing) / strandSpacing;
        const feather = Math.sin(strandPos * Math.PI) * 0.5 + 0.5;
        const falloff = (1 - d) * feather;
        if (falloff < 0.05) continue;
        let totalAlpha = 0, count = 0;
        const k = 2;
        for (let ky = -k; ky <= k; ky++) {
          for (let kx = -k; kx <= k; kx++) {
            const nx = px + kx, ny = py + ky;
            if (nx >= 0 && nx < sw && ny >= 0 && ny < sh) {
              const ni = (ny * sw + nx) * 4;
              if (origData[ni + 3] > 5) { totalAlpha += origData[ni + 3]; count++; }
            }
          }
        }
        if (count === 0) continue;
        const avgAlpha = totalAlpha / count;
        const blend = falloff * strength;
        const blended = Math.round(origAlpha * (1 - blend) + avgAlpha * blend); data[i + 3] = Math.max(origAlpha, blended);
      }
    }
    ctx.putImageData(imageData, sx, sy);
  }
  // Powder brush: big soft circular blur - ONLY smooths existing graphite
  drawPowderBrush(x, y, pressure, from, to) {
    const ctx = this.ctx;
    const size = Math.max(40, this.settings.size * 5);
    const radius = size / 2;
    const strength = 0.03 + pressure * 0.05;
    const sx = Math.max(0, Math.floor(x - radius));
    const sy = Math.max(0, Math.floor(y - radius));
    const sw = Math.min(this.drawingCanvas.width - sx, Math.ceil(size));
    const sh = Math.min(this.drawingCanvas.height - sy, Math.ceil(size));
    if (sw <= 0 || sh <= 0) return;
    const imageData = ctx.getImageData(sx, sy, sw, sh);
    const data = imageData.data;
    const origData = new Uint8ClampedArray(data);
    for (let py = 0; py < sh; py++) {
      for (let px = 0; px < sw; px++) {
        const i = (py * sw + px) * 4;
        const origAlpha = origData[i + 3];
        if (origAlpha < 10) continue;
        const dx = px - sw/2, dy = py - sh/2;
        const dist = Math.sqrt(dx*dx + dy*dy) / radius;
        if (dist > 1) continue;
        const falloff = Math.pow(1 - dist, 2);
        if (falloff < 0.05) continue;
        let totalAlpha = 0, count = 0;
        const kernelSize = Math.ceil(3 + pressure * 3);
        for (let ky = -kernelSize; ky <= kernelSize; ky++) {
          for (let kx = -kernelSize; kx <= kernelSize; kx++) {
            const nx = px + kx, ny = py + ky;
            if (nx >= 0 && nx < sw && ny >= 0 && ny < sh) {
              const ni = (ny * sw + nx) * 4;
              if (origData[ni + 3] > 5) {
                const w = 1 / (1 + Math.sqrt(kx*kx + ky*ky));
                totalAlpha += origData[ni + 3] * w; count += w;
              }
            }
          }
        }
        if (count === 0) continue;
        const avgAlpha = totalAlpha / count;
        const blend = falloff * strength;
        const blended = Math.round(origAlpha * (1 - blend) + avgAlpha * blend); data[i + 3] = Math.max(origAlpha, blended);
      }
    }
    ctx.putImageData(imageData, sx, sy);
  }
  // Tortillon: directional smear - ONLY moves existing graphite
  drawTortillon(x, y, pressure, from, to) {
    const ctx = this.ctx;
    const size = Math.max(8, this.settings.size * 1.5);
    const radius = size / 2;
    const strength = 0.04 + pressure * 0.06;
    const dx = to.x - from.x, dy = to.y - from.y;
    const dist = Math.hypot(dx, dy);
    if (dist < 0.5) return;
    const dirX = dx / dist, dirY = dy / dist;
    const sx = Math.max(0, Math.floor(x - radius * 2));
    const sy = Math.max(0, Math.floor(y - radius * 2));
    const sw = Math.min(this.drawingCanvas.width - sx, Math.ceil(size * 3));
    const sh = Math.min(this.drawingCanvas.height - sy, Math.ceil(size * 3));
    if (sw <= 0 || sh <= 0) return;
    const imageData = ctx.getImageData(sx, sy, sw, sh);
    const data = imageData.data;
    const origData = new Uint8ClampedArray(data);
    const localX = x - sx, localY = y - sy;
    for (let py = 0; py < sh; py++) {
      for (let px = 0; px < sw; px++) {
        const i = (py * sw + px) * 4;
        const origAlpha = origData[i + 3];
        if (origAlpha < 5) continue;
        const bx = px - localX, by = py - localY;
        const d = Math.sqrt(bx*bx + by*by) / radius;
        if (d > 1.5) continue;
        const falloff = Math.max(0, Math.pow(1 - Math.min(d, 1), 2));
        if (falloff < 0.05) continue;
        const sampleDist = 1 + pressure * 2;
        const srcX = Math.round(px - dirX * sampleDist);
        const srcY = Math.round(py - dirY * sampleDist);
        if (srcX >= 0 && srcX < sw && srcY >= 0 && srcY < sh) {
          const si = (srcY * sw + srcX) * 4;
          const srcAlpha = origData[si + 3];
          if (srcAlpha < 5) continue;
          const blend = falloff * strength;
          data[i + 3] = Math.round(origAlpha * (1 - blend) + srcAlpha * blend);
        }
      }
    }
    ctx.putImageData(imageData, sx, sy);
  }
  saveState() {
    if (this.undoStack.length >= this.maxHistory) this.undoStack.shift();
    this.undoStack.push(this.ctx.getImageData(0, 0, this.drawingCanvas.width, this.drawingCanvas.height));
    this.redoStack = []; this.updateStats();
  }
  undo() {
    if (this.undoStack.length === 0) return;
    this.redoStack.push(this.ctx.getImageData(0, 0, this.drawingCanvas.width, this.drawingCanvas.height));
    const state = this.undoStack.pop();
    this.ctx.putImageData(state, 0, 0); this.updateStats();
  }
  redo() {
    if (this.redoStack.length === 0) return;
    this.undoStack.push(this.ctx.getImageData(0, 0, this.drawingCanvas.width, this.drawingCanvas.height));
    const state = this.redoStack.pop();
    this.ctx.putImageData(state, 0, 0); this.updateStats();
  }
  clear() { this.saveState(); this.ctx.clearRect(0, 0, this.drawingCanvas.width, this.drawingCanvas.height); this.updateStats(); }
  save() {
    const exportCanvas = document.createElement('canvas');
    exportCanvas.width = this.drawingCanvas.width; exportCanvas.height = this.drawingCanvas.height;
    const exportCtx = exportCanvas.getContext('2d');
    exportCtx.drawImage(this.paperCanvas, 0, 0);
    exportCtx.drawImage(this.drawingCanvas, 0, 0);
    const link = document.createElement('a');
    link.download = `graphite-${Date.now()}.png`;
    link.href = exportCanvas.toDataURL('image/png'); link.click();
  }
  updateStats() {
    const w = this.drawingCanvas.width, h = this.drawingCanvas.height;
    document.getElementById('stats-display').textContent = `${w}x${h} | Undo: ${this.undoStack.length}`;
  }
}
window.addEventListener('DOMContentLoaded', () => { window.app = new GraphiteApp(); });
