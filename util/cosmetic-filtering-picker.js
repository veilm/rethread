/* 
   RETHREAD ELEMENT PICKER
   Runs in the browser tab. Returns a Promise that resolves to the filter config.
*/
const PICKER_CONFIG = {
  // Controls highlight tweening when moving between selections
  animationsEnabled: false
};

(() => new Promise((resolve) => {
  // --- 1. SETUP SHADOW DOM (Isolation) ---
  const ID = 'rethread-picker-ui';
  if (document.getElementById(ID)) return; // Prevent multiple instances

  const host = document.createElement('div');
  host.id = ID;
  const shadow = host.attachShadow({ mode: 'open' });
  // Ensure the host doesn't block interaction but sits on top
  host.style.cssText = 'position: fixed; top: 0; left: 0; width: 0; height: 0; z-index: 2147483647;';
  document.documentElement.appendChild(host);

  const style = document.createElement('style');
  style.textContent = `
    :host { 
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      color-scheme: dark;
    }
    .panel {
      position: fixed; bottom: 20px; right: 20px; width: 320px;
      background: #1d1f24; color: #f5f5f5; padding: 16px; border-radius: 12px;
      box-shadow: 0 18px 45px rgba(0,0,0,0.55); border: 1px solid #2c2f35;
      display: flex; flex-direction: column; gap: 12px; z-index: 2147483647;
      font-size: 13px;
    }
    .row { display: flex; gap: 8px; align-items: center; }
    .grow { flex: 1; }
    h3 { margin: 0; color: #d1d5db; font-size: 13px; letter-spacing: 0.5px; font-weight: 600; }
    button {
      background: #2b2e35; border: 1px solid #3a3f48; color: #f3f4f6; padding: 6px 10px;
      border-radius: 6px; cursor: pointer; font-size: 12px; transition: 0.2s;
    }
    button:hover { background: #363a42; }
    button.primary { background: #f3f4f6; color: #111; border-color: transparent; font-weight: 600; }
    button.primary:hover { background: #e5e7eb; }
    
    label { display: flex; align-items: center; gap: 6px; cursor: pointer; user-select: none; color: #d1d5db; }
    input[type="range"] { width: 100%; accent-color: #9ca3af; }
    input[type="text"] { 
      width: 100%; background: #111318; border: 1px solid #2f333b; color: #f3f4f6; 
      padding: 6px; border-radius: 4px; box-sizing: border-box; margin-top: 5px;
    }
    input:disabled { opacity: 0.45; cursor: not-allowed; }
    
    .code { 
      font-family: monospace; background: #0d0f14; padding: 8px; border-radius: 6px; 
      color: #cfd3da; word-break: break-all; border: 1px solid #2f333b; font-size: 11px;
    }
    .hint { font-size: 11px; color: #9ca3af; margin-top: 2px; }
    .hint.warning { color: #ef4444; }
    hr { border:0; border-top:1px solid #2c2f35; width:100%; margin:0; }
  `;
  shadow.appendChild(style);

  // UI Components
  const panel = document.createElement('div');
  panel.className = 'panel';
  panel.style.display = 'none'; // Hidden until click
  panel.innerHTML = `
    <div class="row" style="justify-content:space-between">
      <h3>Filter Creator</h3>
      <button id="close">✕</button>
    </div>

    <div class="row">
      <button id="up" class="grow">▲ Select Parent</button>
      <button id="down" class="grow">▼ Select Child</button>
    </div>

    <div>
      <div class="row" style="justify-content:space-between; margin-bottom:4px;">
        <span>Selector Strictness</span>
        <span id="level-label" style="color:#888; font-size:11px">Smart</span>
      </div>
      <input type="range" id="strictness" min="0" max="3" step="1" value="2">
      <div class="code" id="preview"></div>
      <div class="hint">Slide left to make selector more generic (fixes brittle classes)</div>
    </div>

    <hr>

    <div>
      <label>
        <input type="checkbox" id="use-text">
        <strong>Block if text contains...</strong>
      </label>
      <input type="text" id="text-val" disabled placeholder="Enter text to detect">
      <div class="hint warning" id="text-warning" style="display:none;">Original element doesn't contain that text.</div>
    </div>

    <button id="create" class="primary" style="width:100%">Create Filter</button>
  `;
  shadow.appendChild(panel);

  // --- 2. LOGIC ---
  let state = {
    picking: true,      // True = hover highlights, False = locked on element
    element: null,      // The currently selected DOM element
    original: null,     // The element originally clicked (before traversing up)
    selector: ''
  };

  // Highlight layer shared between modes
  let highlightLayer = null;
  let highlightBoxes = [];
  let primaryBox = null;
  let highlightedElements = [];
  let highlightedPrimary = null;
  let viewportListener = null;
  let textCheck = null;
  let textInput = null;
  let textWarning = null;

  function ensureHighlightLayer() {
    if (!highlightLayer) {
      highlightLayer = document.createElement('div');
      Object.assign(highlightLayer.style, {
        position: 'fixed',
        top: 0,
        left: 0,
        width: '100vw',
        height: '100vh',
        pointerEvents: 'none',
        zIndex: 2147483646
      });
      document.body.appendChild(highlightLayer);
    }
    if (!primaryBox) {
      primaryBox = document.createElement('div');
      Object.assign(primaryBox.style, {
        position: 'absolute',
        outline: '2px solid rgba(163, 196, 255, 0.95)',
        background: 'rgba(115, 163, 255, 0.12)',
        transition: PICKER_CONFIG.animationsEnabled ? 'all 0.1s ease' : 'none'
      });
      highlightLayer.appendChild(primaryBox);
    }
  }

  function setHighlights(primary, matches) {
    highlightedPrimary = (primary && primary.isConnected) ? primary : null;
    highlightedElements = (matches || []).filter(Boolean);
    updateHighlightPositions();
  }

  function updateHighlightPositions() {
    if (!highlightedPrimary && highlightedElements.length === 0) {
      clearHighlightLayer();
      return;
    }
    ensureHighlightLayer();

    if (highlightedPrimary) {
      const rect = highlightedPrimary.getBoundingClientRect();
      Object.assign(primaryBox.style, {
        display: 'block',
        top: rect.top + 'px',
        left: rect.left + 'px',
        width: rect.width + 'px',
        height: rect.height + 'px'
      });
    } else if (primaryBox) {
      primaryBox.style.display = 'none';
    }

    while (highlightBoxes.length < highlightedElements.length) {
      const box = document.createElement('div');
      Object.assign(box.style, {
        position: 'absolute',
        outline: '2px dashed rgba(255,255,255,0.6)',
        background: 'rgba(255,255,255,0.08)',
        transition: PICKER_CONFIG.animationsEnabled ? 'all 0.1s ease' : 'none'
      });
      highlightLayer.appendChild(box);
      highlightBoxes.push(box);
    }
    while (highlightBoxes.length > highlightedElements.length) {
      const box = highlightBoxes.pop();
      if (box) box.remove();
    }

    highlightBoxes.forEach((box, idx) => {
      const el = highlightedElements[idx];
      if (el && el.isConnected) {
        const rect = el.getBoundingClientRect();
        Object.assign(box.style, {
          display: 'block',
          top: rect.top + 'px',
          left: rect.left + 'px',
          width: rect.width + 'px',
          height: rect.height + 'px'
        });
      } else {
        box.style.display = 'none';
      }
    });
  }

  function clearHighlightLayer() {
    highlightBoxes.forEach(box => box.remove());
    highlightBoxes = [];
    if (primaryBox) {
      primaryBox.remove();
      primaryBox = null;
    }
    if (highlightLayer) {
      highlightLayer.remove();
      highlightLayer = null;
    }
  }

  function attachViewportListeners() {
    if (!viewportListener) {
      viewportListener = () => updateHighlightPositions();
      window.addEventListener('scroll', viewportListener, true);
      window.addEventListener('resize', viewportListener);
    }
  }

  function detachViewportListeners() {
    if (viewportListener) {
      window.removeEventListener('scroll', viewportListener, true);
      window.removeEventListener('resize', viewportListener);
      viewportListener = null;
    }
  }

  // Generates selectors at different specificity levels
  // 0: Generic Tag (div) - Risk of false positives unless combined with Text
  // 1: Tag + Nth (div:nth-of-type(3)) - Good for structure
  // 2: Smart Class (div.content) - Tries to pick "good" classes, ignores random hashes
  // 3: Strict Path (body > div#app > div.sidebar) - Very specific, brittle
  function getSelectorOptions(el) {
    if (!el) return [];
    const tag = el.tagName.toLowerCase();
    
    // Heuristic: filter out classes that look like garbage (e.g. css-1dbjc4n)
    const goodClasses = Array.from(el.classList)
      .filter(c => c.length > 2 && !/^[0-9]/.test(c) && !c.includes(':'));
    const classSel = goodClasses.length ? `.${goodClasses.join('.')}` : '';
    
    // Nth-of-type
    let nth = '';
    if (el.parentElement) {
      const idx = Array.from(el.parentElement.children)
        .filter(c => c.tagName === el.tagName).indexOf(el) + 1;
      nth = `:nth-of-type(${idx})`;
    }

    // Path generator
    const getPath = (limit) => {
      let path = tag;
      let curr = el;
      for(let i=0; i<limit; i++) {
        if(!curr.parentElement || curr.parentElement === document.body) break;
        curr = curr.parentElement;
        path = `${curr.tagName.toLowerCase()} > ${path}`;
      }
      return path;
    };

    return [
      tag,                         // 0: Loose
      tag + nth,                   // 1: Structural
      tag + classSel,              // 2: Classes (Default)
      getPath(2) + classSel        // 3: Strict
    ];
  }

  function getSuggestedText() {
    const src = state.original || state.element;
    if (!src) return '';
    const raw = (src.innerText || '').split('\n')[0].trim();
    return raw.substring(0, 50);
  }

  function elementContainsText(el, needleLower) {
    if (!el || !needleLower) return false;
    const content = (el.innerText || '').toLowerCase();
    return content.includes(needleLower);
  }

  function getActiveTextFilter() {
    if (!textCheck || !textInput || !textCheck.checked) return '';
    return textInput.value.trim();
  }

  function updateTextWarning(needleLower) {
    if (!textWarning) return;
    if (!needleLower || !textCheck || !textCheck.checked) {
      textWarning.style.display = 'none';
      return;
    }
    const target = state.original || state.element;
    const matches = elementContainsText(target, needleLower);
    textWarning.style.display = matches ? 'none' : 'block';
  }

  function update() {
    if (!state.element) return;
    
    if (state.picking) {
      setHighlights(state.element, []);
      return;
    }

    // Update Preview
    const opts = getSelectorOptions(state.element);
    const val = shadow.getElementById('strictness').value;
    // Fallback if option is empty (e.g. no classes)
    state.selector = opts[val] || opts[1] || opts[0];
    
    shadow.getElementById('preview').textContent = state.selector;

    // Auto-suggest text
    const input = textInput || shadow.getElementById('text-val');
    if (!textInput && input) textInput = input;
    if (input && !input.value) {
      const suggested = getSuggestedText();
      if (suggested) input.value = suggested;
    }

    refreshMatches();
  }

  function refreshMatches() {
    if (!state.selector) {
      setHighlights(state.element, []);
      return;
    }
    let matches = [];
    try {
      matches = Array.from(document.querySelectorAll(state.selector));
    } catch (err) {
      matches = [];
    }
    const filterText = getActiveTextFilter();
    let normalized = '';
    if (filterText) {
      normalized = filterText.toLowerCase();
      matches = matches.filter(el => elementContainsText(el, normalized));
    }
    updateTextWarning(normalized);
    const others = matches.filter(el => el !== state.element);
    setHighlights(state.element, others);
  }

  // Event Handlers
  const onMove = (e) => {
    if (!state.picking) return;
    const el = document.elementFromPoint(e.clientX, e.clientY);
    if (el && !host.contains(el)) {
      state.element = el;
      update();
    }
  };

  const onClick = (e) => {
    if (host.contains(e.target)) return; // Allow interaction with panel
    e.preventDefault();
    e.stopPropagation();
    
    if (state.picking) {
      state.picking = false;
      state.original = state.element;
      panel.style.display = 'flex';
      setHighlights(state.element, []);
      update();
    }
  };

  document.addEventListener('mousemove', onMove);
  document.addEventListener('click', onClick, { capture: true });
  attachViewportListeners();

  // Panel Inputs
  shadow.getElementById('up').onclick = () => {
    if (state.element.parentElement && state.element.parentElement !== document.body) {
      state.element = state.element.parentElement;
      update();
    }
  };
  shadow.getElementById('down').onclick = () => {
    // Try to find the child that contains our original click
    if (state.original && state.element.contains(state.original) && state.element !== state.original) {
       let curr = state.original;
       while (curr.parentElement !== state.element) curr = curr.parentElement;
       state.element = curr;
    } else if (state.element.firstElementChild) {
       state.element = state.element.firstElementChild;
    }
    update();
  };
  
  shadow.getElementById('strictness').oninput = update;
  
  textCheck = shadow.getElementById('use-text');
  textInput = shadow.getElementById('text-val');
  textWarning = shadow.getElementById('text-warning');
  textCheck.onchange = () => {
    textInput.disabled = !textCheck.checked;
    refreshMatches();
  };
  textInput.oninput = () => {
    refreshMatches();
  };

  // Finish
  shadow.getElementById('create').onclick = () => {
    const config = { selector: state.selector };
    
    if (textCheck.checked) {
      // Use the entered (or auto-detected) text value
      const val = textInput.value.trim();
      if (val) config.hasText = val;
    }

    cleanup();
    resolve(config); // Resolves the Promise -> Rethread CLI prints this object
  };

  shadow.getElementById('close').onclick = () => {
    cleanup();
    resolve(null);
  };

  function cleanup() {
    document.removeEventListener('mousemove', onMove);
    document.removeEventListener('click', onClick, { capture: true });
    detachViewportListeners();
    clearHighlightLayer();
    host.remove();
  }
}))();
