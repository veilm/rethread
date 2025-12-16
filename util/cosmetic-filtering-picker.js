(function() {
    // --- 1. CONFIGURATION & HELPERS ---
    
    // Helper to create DOM elements safely (bypassing innerHTML Trusted Types)
    const h = (tag, styles = {}, text = '') => {
        const el = document.createElement(tag);
        Object.assign(el.style, styles);
        if (text) el.textContent = text;
        return el;
    };

    let originalTarget = null; // What the user actually clicked
    let currentTarget = null;  // What the user is currently selecting (ancestor)
    let pickerOverlay = null;  // The highlighter box
    let uiRoot = null;         // The shadow DOM container

    // --- 2. SELECTOR LOGIC ---

    // Generates different flavors of selectors for a given element
    function generateSelectors(el) {
        if (!el || el.nodeName === 'BODY' || el.nodeName === 'HTML') return ['body'];
        
        const candidates = [];
        
        // Option A: ID (Best if available and looks static)
        if (el.id) {
            candidates.push({ label: 'ID', value: '#' + el.id });
        }
        
        // Option B: Class (Good, but watch out for random strings)
        if (el.className && typeof el.className === 'string') {
            const classes = el.className.trim().split(/\s+/).filter(c => c.length > 0);
            if (classes.length > 0) {
                candidates.push({ label: 'Class', value: '.' + classes.join('.') });
            }
        }
        
        // Option C: Structural Path (Fallback)
        let path = el.nodeName.toLowerCase();
        let sibling = el;
        let nth = 1;
        while (sibling = sibling.previousElementSibling) {
            if (sibling.nodeName === el.nodeName) nth++;
        }
        if (nth > 1) path += `:nth-of-type(${nth})`;
        
        // Minimal recursive path for context
        if (el.parentElement) {
             const parentTag = el.parentElement.nodeName.toLowerCase();
             candidates.push({ label: 'Structure', value: `${parentTag} > ${path}` });
        } else {
             candidates.push({ label: 'Structure', value: path });
        }

        return candidates;
    }

    // --- 3. UI BUILDER ---

    function showUI(targetEl) {
        originalTarget = targetEl;
        currentTarget = targetEl;
        
        // Create Host Container
        const host = document.createElement('div');
        host.id = 'rethread-ui-host';
        Object.assign(host.style, {
            position: 'fixed', bottom: '20px', right: '20px', zIndex: '9999999',
            fontFamily: 'sans-serif', fontSize: '14px'
        });
        document.body.appendChild(host);
        
        // Create Shadow DOM
        const shadow = host.attachShadow({mode: 'open'});
        uiRoot = host;

        // Container Box
        const container = h('div', {
            backgroundColor: '#222', color: '#fff', padding: '16px',
            borderRadius: '8px', boxShadow: '0 4px 12px rgba(0,0,0,0.5)',
            width: '300px', display: 'flex', flexDirection: 'column', gap: '12px'
        });
        
        // -- DEPTH CONTROL --
        const depthRow = h('div', { display: 'flex', flexDirection: 'column', gap: '4px'});
        depthRow.appendChild(h('label', { fontSize: '12px', color: '#aaa'}, 'Depth (Parent Level)'));
        const depthSlider = h('input');
        depthSlider.type = 'range';
        depthSlider.min = 0; 
        depthSlider.max = 4; // Allow going up 4 parents
        depthSlider.value = 0;
        depthRow.appendChild(depthSlider);
        container.appendChild(depthRow);

        // -- SPECIFICITY CONTROL --
        const specRow = h('div', { display: 'flex', flexDirection: 'column', gap: '4px'});
        specRow.appendChild(h('label', { fontSize: '12px', color: '#aaa'}, 'Specificity'));
        const specSelect = h('select', { padding: '4px', backgroundColor: '#333', color: 'white', border: '1px solid #555' });
        specRow.appendChild(specSelect);
        container.appendChild(specRow);

        // -- PREVIEW TEXT AREA --
        const textArea = h('textarea', {
            width: '100%', height: '50px', backgroundColor: '#111', color: '#0f0',
            border: '1px solid #444', fontFamily: 'monospace', fontSize: '12px', resize: 'none'
        });
        container.appendChild(textArea);

        // -- BUTTONS --
        const btnRow = h('div', { display: 'flex', gap: '8px', justifyContent: 'flex-end'});
        const cancelBtn = h('button', { cursor: 'pointer', padding: '6px 12px', background: 'transparent', color: '#fff', border: '1px solid #555', borderRadius: '4px'}, 'Cancel');
        const createBtn = h('button', { cursor: 'pointer', padding: '6px 12px', background: '#3b82f6', color: '#fff', border: 'none', borderRadius: '4px', fontWeight: 'bold'}, 'Create');
        btnRow.appendChild(cancelBtn);
        btnRow.appendChild(createBtn);
        container.appendChild(btnRow);

        shadow.appendChild(container);

        // --- 4. STATE LOGIC ---

        const updateState = () => {
            // 1. Calculate Target based on Depth
            let level = parseInt(depthSlider.value);
            let el = originalTarget;
            for (let i = 0; i < level; i++) {
                if (el.parentElement && el.parentElement.tagName !== 'BODY') {
                    el = el.parentElement;
                }
            }
            currentTarget = el;

            // 2. Highlight the new target
            highlight(currentTarget);

            // 3. Generate options for this target
            const options = generateSelectors(currentTarget);
            
            // 4. Update Dropdown (preserve selection if possible)
            const currentVal = specSelect.value;
            specSelect.innerHTML = ''; // Clear old options
            options.forEach(opt => {
                const o = document.createElement('option');
                o.value = opt.value;
                o.textContent = `${opt.label}: ${opt.value.substring(0, 20)}...`;
                specSelect.appendChild(o);
            });
            
            // Auto-select best option or previous
            if (options.some(o => o.value === currentVal)) {
                specSelect.value = currentVal;
            } else if (options.length > 0) {
                specSelect.value = options[0].value;
            } else {
                specSelect.value = '';
            }

            // 5. Update Text Area
            textArea.value = specSelect.value;
        };

        // Listeners
        depthSlider.oninput = updateState;
        specSelect.onchange = () => { textArea.value = specSelect.value; };
        
        cancelBtn.onclick = () => {
            cleanup();
            console.log(JSON.stringify({ status: "cancelled" }));
        };
        
        createBtn.onclick = () => {
            console.log(JSON.stringify({
                status: "success",
                selector: textArea.value,
                finalTargetTag: currentTarget.tagName
            }));
            cleanup();
        };

        // Initialize
        updateState();
    }

    // --- 5. CORE HIGHLIGHTER ---
    
    function highlight(el) {
        if (!pickerOverlay) {
            pickerOverlay = document.createElement('div');
            Object.assign(pickerOverlay.style, {
                position: 'fixed', pointerEvents: 'none', zIndex: '9999998',
                outline: '2px solid #f00', background: 'rgba(255,0,0,0.1)', transition: 'all 0.1s ease'
            });
            document.body.appendChild(pickerOverlay);
        }
        const rect = el.getBoundingClientRect();
        Object.assign(pickerOverlay.style, {
            top: rect.top + 'px', left: rect.left + 'px',
            width: rect.width + 'px', height: rect.height + 'px'
        });
    }

    // --- 6. INITIAL PICKER MODE ---

    function startPicker() {
        // Overlay for initial selection
        const cover = document.createElement('div');
        Object.assign(cover, { id: 'rethread-cover' });
        Object.assign(cover.style, {
            position: 'fixed', top: 0, left: 0, width: '100vw', height: '100vh',
            zIndex: '9999997', cursor: 'crosshair'
        });
        document.body.appendChild(cover);

        const onMove = (e) => {
            cover.style.display = 'none'; // Hide cover to get element below
            const el = document.elementFromPoint(e.clientX, e.clientY);
            cover.style.display = 'block'; // Restore cover
            if (el) highlight(el);
        };

        const onClick = (e) => {
            cover.style.display = 'none';
            const el = document.elementFromPoint(e.clientX, e.clientY);
            
            // Stop Picker Mode, Start UI Mode
            document.removeEventListener('mousemove', onMove);
            document.removeEventListener('click', onClick);
            cover.remove();
            
            if (el) showUI(el);
        };

        document.addEventListener('mousemove', onMove);
        document.addEventListener('click', onClick);
        
        // Escape to exit
        document.addEventListener('keydown', function esc(e) {
            if (e.key === 'Escape') {
                cleanup();
                document.removeEventListener('keydown', esc);
            }
        });
    }

    function cleanup() {
        if (pickerOverlay) pickerOverlay.remove();
        if (uiRoot) uiRoot.remove();
        const cover = document.getElementById('rethread-cover');
        if (cover) cover.remove();
    }

    // START
    startPicker();
})();
