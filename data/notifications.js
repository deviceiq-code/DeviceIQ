// Shared by every authenticated page except dashboard.html for the
// component-change poller (the dashboard already shows live state inline).
// The toast/confirm primitives are used on every page, dashboard included -
// e.g. for the "Restart" button.
(function () {
    const POLL_INTERVAL_MS = 3000;
    const TOAST_DURATION_MS = 5000;

    let toastContainer = null;
    let modalOverlay = null;
    let previousComponents = null;
    let pollTimer = null;

    function ensureToastContainer() {
        if (toastContainer) return toastContainer;
        toastContainer = document.createElement("div");
        toastContainer.className = "toast-container";
        document.body.appendChild(toastContainer);
        return toastContainer;
    }

    function show(message) {
        const container = ensureToastContainer();
        const toast = document.createElement("div");
        toast.className = "toast";
        toast.textContent = message;
        container.appendChild(toast);

        requestAnimationFrame(function () { toast.classList.add("visible"); });
        setTimeout(function () {
            toast.classList.remove("visible");
            setTimeout(function () { toast.remove(); }, 300);
        }, TOAST_DURATION_MS);
    }

    function ensureModal() {
        if (modalOverlay) return modalOverlay;
        modalOverlay = document.createElement("div");
        modalOverlay.className = "modal-overlay";
        modalOverlay.innerHTML =
            '<div class="modal-box">' +
                '<div class="modal-message"></div>' +
                '<div class="modal-actions">' +
                    '<button class="secondary" data-role="cancel"></button>' +
                    '<button data-role="confirm"></button>' +
                '</div>' +
            '</div>';
        document.body.appendChild(modalOverlay);
        return modalOverlay;
    }

    // Resolves true if the user confirmed, false if cancelled or dismissed.
    function confirmDialog(message, options) {
        options = options || {};
        const overlay = ensureModal();
        const confirmButton = overlay.querySelector('[data-role="confirm"]');
        const cancelButton = overlay.querySelector('[data-role="cancel"]');
        overlay.querySelector(".modal-message").textContent = message;
        confirmButton.textContent = options.confirmLabel || "OK";
        cancelButton.textContent = options.cancelLabel || "Cancel";

        return new Promise(function (resolve) {
            function cleanup(result) {
                overlay.classList.remove("visible");
                confirmButton.removeEventListener("click", onConfirm);
                cancelButton.removeEventListener("click", onCancel);
                overlay.removeEventListener("click", onOverlayClick);
                resolve(result);
            }
            function onConfirm() { cleanup(true); }
            function onCancel() { cleanup(false); }
            function onOverlayClick(event) { if (event.target === overlay) cleanup(false); }

            confirmButton.addEventListener("click", onConfirm);
            cancelButton.addEventListener("click", onCancel);
            overlay.addEventListener("click", onOverlayClick);
            overlay.classList.add("visible");
        });
    }

    function describeChange(before, after) {
        if (after.class === "Relay") {
            if (before.state !== after.state) return after.name + " turned " + (after.state ? "on" : "off");
        } else if (after.class === "Button") {
            if (before.state !== after.state) return after.name + " " + (after.state ? "pressed" : "released");
        } else if (after.class === "Blinds") {
            if (before.state !== after.state || before.position !== after.position) {
                return after.name + ": " + after.state + " (" + after.position + "%)";
            }
        } else if (after.class === "Thermometer") {
            if (before.available !== after.available) {
                return after.name + (after.available ? " is reporting again" : " became unavailable");
            }
        }
        return null;
    }

    function poll() {
        fetch("/api/components").then(function (response) {
            return response.ok ? response.json() : null;
        }).then(function (data) {
            if (!data) return;
            const current = {};
            (data.components || []).forEach(function (item) { current[item.id] = item; });

            if (previousComponents) {
                Object.keys(current).forEach(function (id) {
                    const before = previousComponents[id];
                    if (!before) return;
                    const message = describeChange(before, current[id]);
                    if (message) show(message);
                });
            }

            previousComponents = current;
        }).catch(function () {});
    }

    window.Notifications = {
        show: show,
        confirm: confirmDialog,
        // Starts polling for component state changes and toasting them.
        // Call only on pages that do not already show live component state
        // (i.e. not dashboard.html).
        start: function () {
            if (pollTimer) return;
            poll();
            pollTimer = setInterval(poll, POLL_INTERVAL_MS);
        },
        stop: function () {
            if (pollTimer) clearInterval(pollTimer);
            pollTimer = null;
        }
    };
})();
