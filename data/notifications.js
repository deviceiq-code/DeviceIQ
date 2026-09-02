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

    // options.title is shown above the message; toasts stack (each call
    // adds one to the container) and can be dismissed early via the close
    // button, in addition to auto-dismissing after TOAST_DURATION_MS.
    function show(message, options) {
        options = options || {};
        const container = ensureToastContainer();

        const toast = document.createElement("div");
        toast.className = "toast";

        const content = document.createElement("div");
        content.className = "toast-content";
        if (options.title) {
            const title = document.createElement("div");
            title.className = "toast-title";
            title.textContent = options.title;
            content.appendChild(title);
        }
        const messageEl = document.createElement("div");
        messageEl.className = "toast-message";
        messageEl.textContent = message;
        content.appendChild(messageEl);

        const closeButton = document.createElement("button");
        closeButton.type = "button";
        closeButton.className = "toast-close";
        closeButton.setAttribute("aria-label", "Dismiss");
        closeButton.textContent = "×";

        toast.appendChild(content);
        toast.appendChild(closeButton);
        container.appendChild(toast);

        let dismissTimer = null;
        function dismiss() {
            if (dismissTimer) clearTimeout(dismissTimer);
            toast.classList.remove("visible");
            setTimeout(function () { toast.remove(); }, 300);
        }
        closeButton.addEventListener("click", dismiss);

        requestAnimationFrame(function () { toast.classList.add("visible"); });
        dismissTimer = setTimeout(dismiss, TOAST_DURATION_MS);
    }

    function ensureModal() {
        if (modalOverlay) return modalOverlay;
        modalOverlay = document.createElement("div");
        modalOverlay.className = "modal-overlay";
        modalOverlay.innerHTML =
            '<div class="modal-box">' +
                '<div class="modal-message"></div>' +
                '<input class="modal-input" type="text" hidden>' +
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
        overlay.querySelector(".modal-input").hidden = true;
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

    // Resolves the entered text if confirmed, or null if cancelled/dismissed.
    function promptDialog(message, options) {
        options = options || {};
        const overlay = ensureModal();
        const confirmButton = overlay.querySelector('[data-role="confirm"]');
        const cancelButton = overlay.querySelector('[data-role="cancel"]');
        const input = overlay.querySelector(".modal-input");
        overlay.querySelector(".modal-message").textContent = message;
        confirmButton.textContent = options.confirmLabel || "OK";
        cancelButton.textContent = options.cancelLabel || "Cancel";
        input.type = options.inputType || "text";
        input.value = options.defaultValue || "";
        input.hidden = false;

        return new Promise(function (resolve) {
            function cleanup(result) {
                overlay.classList.remove("visible");
                input.hidden = true;
                confirmButton.removeEventListener("click", onConfirm);
                cancelButton.removeEventListener("click", onCancel);
                overlay.removeEventListener("click", onOverlayClick);
                input.removeEventListener("keydown", onKeydown);
                resolve(result);
            }
            function onConfirm() { cleanup(input.value); }
            function onCancel() { cleanup(null); }
            function onOverlayClick(event) { if (event.target === overlay) cleanup(null); }
            function onKeydown(event) { if (event.key === "Enter") { event.preventDefault(); cleanup(input.value); } }

            confirmButton.addEventListener("click", onConfirm);
            cancelButton.addEventListener("click", onCancel);
            overlay.addEventListener("click", onOverlayClick);
            input.addEventListener("keydown", onKeydown);
            overlay.classList.add("visible");
            input.focus();
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
                    if (message) show(message, { title: "Component Update" });
                });
            }

            previousComponents = current;
        }).catch(function () {});
    }

    window.Notifications = {
        show: show,
        confirm: confirmDialog,
        prompt: promptDialog,
        // Navigates to the dedicated "DeviceIQ is restarting..." page,
        // which waits for the device to come back online and then sends
        // the browser to the login page - used everywhere a restart is
        // triggered, instead of just toasting and leaving the user on a
        // page that's about to go unreachable.
        restarting: function () {
            window.location.href = "/restarting.html";
        },
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
