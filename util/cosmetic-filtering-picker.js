(function() {
    // 1. STATE MANAGEMENT
    let lastTarget = null;
    
    // 2. STYLING (The Fix: Using textContent instead of innerHTML)
    const styleID = 'rethread-picker-style';
    if (!document.getElementById(styleID)) {
        const style = document.createElement('style');
        style.id = styleID;
        
        // FIX: strict sites block innerHTML. textContent is safe.
        style.textContent = `
            .rethread-highlight {
                outline: 2px solid #ff4444 !important;
                background-color: rgba(255, 68, 68, 0.1) !important;
                cursor: crosshair !important;
                box-shadow: 0 0 10px rgba(255, 0, 0, 0.5) !important;
            }
            iframe { pointer-events: none; } 
        `;
        document.head.appendChild(style);
    }

    // 3. SELECTOR GENERATOR ENGINE (Basic)
    function getCssPath(el) {
        if (!(el instanceof Element)) return;
        const path = [];
        while (el.nodeType === Node.ELEMENT_NODE) {
            let selector = el.nodeName.toLowerCase();
            if (el.id) {
                selector += '#' + el.id;
                path.unshift(selector);
                break; 
            } else {
                let sibling = el;
                let nth = 1;
                while (sibling = sibling.previousElementSibling) {
                    if (sibling.nodeName.toLowerCase() == selector)
                        nth++;
                }
                if (nth != 1)
                    selector += ":nth-of-type(" + nth + ")";
            }
            path.unshift(selector);
            el = el.parentNode;
        }
        return path.join(" > ");
    }

    // 4. CLEANUP UTILITY
    function cleanup() {
        if (lastTarget) lastTarget.classList.remove('rethread-highlight');
        document.removeEventListener('mouseover', onMouseOver);
        document.removeEventListener('mouseout', onMouseOut);
        document.removeEventListener('click', onClick, true); // remove capture listener
        document.removeEventListener('keydown', onKeydown);
        const style = document.getElementById(styleID);
        if (style) style.remove();
        console.log("Picker mode deactivated.");
    }

    // 5. EVENT HANDLERS
    function onMouseOver(e) {
        e.stopPropagation();
        // Remove highlight from previous element if it exists
        if (lastTarget) lastTarget.classList.remove('rethread-highlight');
        
        lastTarget = e.target;
        e.target.classList.add('rethread-highlight');
    }

    function onMouseOut(e) {
        e.stopPropagation();
        e.target.classList.remove('rethread-highlight');
    }

    function onClick(e) {
        e.preventDefault(); 
        e.stopPropagation(); 
        
        const selector = getCssPath(e.target);
        
        console.log(JSON.stringify({
            status: "success",
            selector: selector,
            tagName: e.target.tagName
        }));
        
        cleanup();
    }

    function onKeydown(e) {
        if (e.key === 'Escape') {
            console.log(JSON.stringify({ status: "cancelled" }));
            cleanup();
        }
    }

    // 6. INITIALIZATION
    document.addEventListener('mouseover', onMouseOver);
    document.addEventListener('mouseout', onMouseOut);
    document.addEventListener('click', onClick, { capture: true }); 
    document.addEventListener('keydown', onKeydown);

    console.log("Picker mode active. Click to select.");
})();
