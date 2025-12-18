// ==UserScript==
// @name        Rethread Cosmetic Filter
// @match       __RETHREAD_PICKER_MATCH__
// @run-at      document-start
// ==/UserScript==
// Helper template used by cosmetic-filters.py. Keep the @match target and
// CONFIG initializer anchors intact so the helper can rewrite them.

(function() {
    /* 
       1. CONFIGURATION
       This is where your browser injects the config for the current domain.
       Example:
       const CONFIG = [
         { selector: ".ad-banner" }, 
         { selector: "div[role='complementary'] > div", hasText: "You might like" }
       ];
    */
    let CONFIG = [];
    try {
        CONFIG = __RETHREAD_PICKER_CONFIG__;
    } catch (err) {
        CONFIG = [];
    }

    if (!CONFIG || !CONFIG.length) return;

    // --- Engine 1: Static CSS (Fastest) ---
    // Filters without 'hasText' are compiled to a raw stylesheet.
    const cssRules = CONFIG.filter(r => !r.hasText);
    if (cssRules.length) {
        const existing = document.getElementById('rethread-css');
        if (!existing) {
            const style = document.createElement('style');
            style.id = 'rethread-css';
            style.textContent = cssRules.map(r => `${r.selector} { display: none !important; }`).join('\n');
            const install = () => {
                const target = document.head || document.documentElement;
                if (!target) {
                    setTimeout(install, 0);
                    return;
                }
                target.appendChild(style);
            };
            install();
        }
    }

    // --- Engine 2: Dynamic Text Watcher (Smart) ---
    // Filters with 'hasText' must be checked via JS.
    const textRules = CONFIG.filter(r => r.hasText);
    
    if (textRules.length) {
        
        const applyTextRules = () => {
            textRules.forEach(rule => {
                // 1. Find Candidates: Use the CSS selector to narrow scope efficiently
                const candidates = document.querySelectorAll(rule.selector);
                
                candidates.forEach(el => {
                    // Optimization: Skip if already hidden
                    if (el.dataset.rethreadHidden) return;

                    // 2. Check Text: innerText is expensive but accurate for visual blocking
                    // We check if the element's visible text contains the keyword
                    if (el.innerText && el.innerText.includes(rule.hasText)) {
                        el.style.setProperty('display', 'none', 'important');
                        el.dataset.rethreadHidden = 'true'; // Mark as handled
                    }
                });
            });
        };

        // Run immediately
        document.addEventListener('DOMContentLoaded', applyTextRules);
        window.addEventListener('load', applyTextRules);

        // Run on DOM Updates (Debounced)
        // This ensures we catch content on infinite-scroll sites without freezing the UI
        let timer;
        const observer = new MutationObserver((mutations) => {
            const hasAdditions = mutations.some(m => m.addedNodes.length > 0);
            if (hasAdditions) {
                if (timer) clearTimeout(timer);
                timer = setTimeout(applyTextRules, 150); // 150ms debounce
            }
        });

        observer.observe(document.documentElement, { childList: true, subtree: true });
    }
})();
