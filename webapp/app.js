/**
 * app.js
 *
 * Entry script for the dashboard:
 *  - wires up DOM event listeners
 *  - periodically polls device properties
 *  - submits property updates
 *  - keeps the UI status and activity log in sync
 */

// DOM element references (assigned during init)
let statusGridEl;
let pollIntervalEl;
let formEl;
let formStatusEl;
let logListEl;
let manualRefreshBtn;
let lastUpdatedEl;
// Balance tracking display elements
let trackC1El, trackC2El, trackC3El;
let prevC1El, prevC2El, prevC3El;

// Cache of the latest snapshot so we can reuse it when needed
let latestSnapshot = null;

// Track previous charge values to detect changes
let previousCharges = {
  c1Charge: null,
  c2Charge: null,
  c3Charge: null,
};

/**
 * Initialize the page once the DOM content is ready.
 */
function init() {
  statusGridEl = document.getElementById("status-grid");
  pollIntervalEl = document.getElementById("poll-interval");
  formEl = document.getElementById("device-form");
  formStatusEl = document.getElementById("form-status");
  logListEl = document.getElementById("log-list");
  manualRefreshBtn = document.getElementById("manual-refresh");
  lastUpdatedEl = document.getElementById("last-updated");
  
  // Balance tracking elements
  trackC1El = document.getElementById("track-c1");
  trackC2El = document.getElementById("track-c2");
  trackC3El = document.getElementById("track-c3");
  prevC1El = document.getElementById("prev-c1");
  prevC2El = document.getElementById("prev-c2");
  prevC3El = document.getElementById("prev-c3");

  // Display the polling interval
  pollIntervalEl.textContent = OneNetApi.config.pollingIntervalSec.toString();

  // Register listeners
  formEl.addEventListener("submit", onSubmitForm);
  manualRefreshBtn.addEventListener("click", () => {
    appendLog("Manual refresh requested");
    fetchAndRender();
  });

  // Fetch immediately on first load
  fetchAndRender();

  // Kick off the polling loop
  startPolling();
}

/**
 * Polling loop. We keep it simple with setInterval for now.
 * A production deployment could switch to WebSocket/SSE later.
 */
function startPolling() {
  setInterval(() => {
    appendLog("Automatic refresh triggered");
    fetchAndRender();
  }, OneNetApi.config.pollingIntervalSec * 1000);
}

/**
 * Pull the latest properties and refresh the UI.
 */
async function fetchAndRender() {
  toggleLoading(true);

  try {
    const data = await OneNetApi.fetchDeviceProperties();
    latestSnapshot = data;
    
    // Check for charge changes and trigger vehicle calls if needed
    checkChargeChanges(data);
    
    renderStatus(data);
    renderFormValues(data);
    formStatusEl.textContent = "Latest data synchronized";
  } catch (error) {
    console.error(error);
    appendLog(`Fetch failed: ${error.message}`, "error");
    formStatusEl.textContent = "Unable to fetch data. Please try again shortly.";
  } finally {
    toggleLoading(false);
  }
}

/**
 * Check for charge changes and call vehicles accordingly.
 * @param {{c1Charge: string, c2Charge: string, c3Charge: string}} data
 */
async function checkChargeChanges(data) {
  // Check if VehicleApi is available
  if (typeof VehicleApi === "undefined") {
    console.error("VehicleApi is not defined! Make sure vehicleApi.js is loaded.");
    appendLog("Error: VehicleApi not loaded. Check console for details.", "error");
    return;
  }

  // Helper to parse charge value (handle "--", null, undefined, or numeric strings)
  const parseCharge = (value) => {
    if (value === null || value === undefined || value === "--" || value === "") {
      return null;
    }
    const num = Number(value);
    return isNaN(num) ? null : num;
  };

  // Check each card's charge
  const chargeMappings = [
    { chargeKey: "c1Charge", cardKey: "card1" },
    { chargeKey: "c2Charge", cardKey: "card2" },
    { chargeKey: "c3Charge", cardKey: "card3" },
  ];

  console.log("=== Checking charge changes ===");
  console.log("Current data:", {
    c1Charge: data.c1Charge,
    c2Charge: data.c2Charge,
    c3Charge: data.c3Charge,
  });
  console.log("Previous charges:", { ...previousCharges });

  for (const { chargeKey, cardKey } of chargeMappings) {
    const rawValue = data[chargeKey];
    const currentCharge = parseCharge(rawValue);
    const previousCharge = previousCharges[chargeKey];

    // Debug log for each card
    console.log(
      `[${cardKey}] Raw: "${rawValue}", Parsed: ${currentCharge}, Previous: ${previousCharge}`
    );

    // Log status for debugging
    if (currentCharge === null && previousCharge === null) {
      console.log(`[${cardKey}] No valid charge data yet (both null)`);
    } else if (currentCharge === null) {
      console.log(`[${cardKey}] Current charge is invalid/null, skipping`);
    } else if (previousCharge === null) {
      console.log(
        `[${cardKey}] First valid charge detected: ${currentCharge} (will trigger on next change)`
      );
      appendLog(
        `[${cardKey.toUpperCase()}] 首次检测到余额: ${currentCharge} (下次变化时将触发车辆调用)`,
        "info"
      );
    } else if (currentCharge === previousCharge) {
      console.log(`[${cardKey}] No change: ${currentCharge} (same as previous)`);
    } else {
      // Change detected!
      console.log(
        `[${cardKey}] ⚠️ CHANGE DETECTED: ${previousCharge} → ${currentCharge}`
      );
      const mapping = VehicleApi.config.cardMapping[cardKey];
      appendLog(
        `[${cardKey.toUpperCase()}] 余额变化: ${previousCharge} → ${currentCharge}. 正在调用车辆 ${mapping.vehicle} 到站 ${mapping.station}`,
        "info"
      );

      try {
        await VehicleApi.callVehicleByCard(cardKey);
        appendLog(
          `✓ 成功调用车辆 ${mapping.vehicle} 到站 ${mapping.station}`,
          "info"
        );
        console.log(
          `✓ Successfully called vehicle ${mapping.vehicle} to station ${mapping.station}`
        );
      } catch (error) {
        console.error(`Failed to call vehicle for ${cardKey}:`, error);
        appendLog(
          `✗ 调用车辆失败 (${cardKey}): ${error.message}`,
          "error"
        );
      }
    }

    // Update the previous charge value
    previousCharges[chargeKey] = currentCharge;
  }

  console.log("Updated previous charges:", { ...previousCharges });
  console.log("=== End charge check ===\n");
  
  // Update the balance tracking display
  updateBalanceTrackingDisplay(data);
}

/**
 * Update the balance tracking display in the UI
 * @param {{c1Charge: string, c2Charge: string, c3Charge: string}} data
 */
function updateBalanceTrackingDisplay(data) {
  const formatValue = (value) => {
    if (value === null || value === undefined || value === "--" || value === "") {
      return "--";
    }
    return String(value);
  };

  if (trackC1El) trackC1El.textContent = formatValue(data.c1Charge);
  if (trackC2El) trackC2El.textContent = formatValue(data.c2Charge);
  if (trackC3El) trackC3El.textContent = formatValue(data.c3Charge);
  
  if (prevC1El) prevC1El.textContent = formatValue(previousCharges.c1Charge);
  if (prevC2El) prevC2El.textContent = formatValue(previousCharges.c2Charge);
  if (prevC3El) prevC3El.textContent = formatValue(previousCharges.c3Charge);
}

/**
 * Render the device status tiles.
 * @param {{card1: string, card2: string, card3: string, c1Charge: string, updatedAt: string}} data
 */
function renderStatus(data) {
  const entries = [
    { label: "Card 1", value: data.card1 },
    { label: "Card 2", value: data.card2 },
    { label: "Card 3", value: data.card3 },
  ];

  statusGridEl.innerHTML = entries
    .map(
      (item) => `
        <div class="status-item">
          <span class="status-item__label">${item.label}</span>
          <span class="status-item__value">${escapeHtml(item.value ?? "--")}</span>
        </div>
      `
    )
    .join("");

  if (lastUpdatedEl) {
    lastUpdatedEl.textContent = data.updatedAt ?? "--";
  }
}

/**
 * Populate the form inputs with the latest charge values.
 * @param {{c1Charge: string, c2Charge: string, c3Charge: string}} data
 */
function renderFormValues(data) {
  const c1Input = document.getElementById("c1Charge");
  const c2Input = document.getElementById("c2Charge");
  const c3Input = document.getElementById("c3Charge");

  if (c1Input) c1Input.value = normalizeChargeValue(data.c1Charge);
  if (c2Input) c2Input.value = normalizeChargeValue(data.c2Charge);
  if (c3Input) c3Input.value = normalizeChargeValue(data.c3Charge);
}

/**
 * Convert placeholder tokens such as "--" into empty strings for inputs.
 * @param {string} value
 */
function normalizeChargeValue(value) {
  if (value === undefined || value === null || value === "--") {
    return "";
  }
  return String(value);
}

/**
 * Form submission handler:
 *  - prevent default submission
 *  - build the payload (skip untouched fields)
 *  - call the API
 *  - update log and status banner
 */
async function onSubmitForm(event) {
  event.preventDefault();

  const formData = new FormData(formEl);
  const payload = {};
  const errors = [];

  ["c1Charge", "c2Charge", "c3Charge"].forEach((key) => {
    const rawValue = formData.get(key);
    if (rawValue === null || rawValue === "") {
      return;
    }

    const cleaned = String(rawValue).trim();
    const parsed = Number(cleaned);

    if (
      Number.isNaN(parsed) ||
      !Number.isInteger(parsed) ||
      parsed < 0 ||
      parsed > 999
    ) {
      errors.push(`${key.toUpperCase()} must be an integer between 0 and 999.`);
      return;
    }

    payload[key] = parsed;
  });

  if (errors.length > 0) {
    const message = errors.join(" ");
    appendLog(`Validation failed: ${message}`, "error");
    formStatusEl.textContent = message;
    return;
  }

  formStatusEl.textContent = "Submitting…";

  try {
    await OneNetApi.updateDeviceProperties(payload);
    appendLog("Update succeeded");
    formStatusEl.textContent = "Update succeeded. Refreshing…";
    // Refresh immediately so the UI stays in sync
    await fetchAndRender();
  } catch (error) {
    console.error(error);
    appendLog(`Update failed: ${error.message}`, "error");
    formStatusEl.textContent =
      "Update failed. Check your input or try again later.";
  }
}

/**
 * Append a line to the activity log.
 * @param {string} message
 * @param {"info"|"error"} level
 */
function appendLog(message, level = "info") {
  const li = document.createElement("li");
  li.className = "log-item";

  if (level === "error") {
    li.style.color = "var(--color-danger)";
  }

  const timeStamp = new Date().toLocaleTimeString();
  li.innerHTML = `<span class="log-item__time">${timeStamp}</span>${escapeHtml(
    message
  )}`;

  logListEl.prepend(li);
}

/**
 * Toggle loading state (disable buttons while we wait).
 * @param {boolean} loading
 */
function toggleLoading(loading) {
  manualRefreshBtn.disabled = loading;
  formEl.querySelector("button[type=submit]").disabled = loading;
}

/**
 * Escape HTML entities to avoid XSS when using innerHTML.
 * @param {string} unsafe
 */
function escapeHtml(unsafe) {
  if (typeof unsafe !== "string") {
    return unsafe;
  }
  return unsafe
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#039;");
}

// Boot the app once the DOM is ready
document.addEventListener("DOMContentLoaded", init);

