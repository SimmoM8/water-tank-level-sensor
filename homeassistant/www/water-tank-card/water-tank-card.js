/* water-tank-card.js
 * Custom Home Assistant Lovelace card (no build step).
 * v0.5.0: device_id auto-resolve for one-click install.
 */

const CARD_TAG = "water-tank-card";
const VERSION = "0.5.10";

// Toast timing (ms)
const TOAST_RESULT_MS = 2200;
const OTA_UI_STALE_MS = 25000;
const OTA_UI_TIMEOUT_MS = 20 * 60 * 1000;

// Diagnostics numeric precision
const DIAGNOSTICS_DECIMALS = 10;

// Main card numeric precision
const CARD_PERCENT_DECIMALS = 0;
const CARD_LITERS_DECIMALS = 1;
const CARD_HEIGHT_DECIMALS = 1;

// Required logical entity keys for the card to render.
const REQUIRED_ENTITY_KEYS = [
  "percent_entity",
  "liters_entity",
  "cm_entity",
  "status_entity",
  "probe_entity",
  "percent_valid_entity",
  "calibration_entity",
  "raw_entity",
];

// Optional logical entity keys we attempt to auto-resolve when a device_id is provided.
const OPTIONAL_ENTITY_KEYS = [
  "quality_reason_entity",
  "tank_volume_entity",
  "rod_length_entity",
  "calibrate_dry_entity",
  "calibrate_wet_entity",
  "clear_calibration_entity",
  "wipe_wifi_entity",
  "ota_pull_entity",
  "ota_state_entity",
  "ota_progress_entity",
  "ota_error_entity",
  "ota_last_status_entity",
  "ota_last_message_entity",
  "update_available_entity",
  "simulation_mode_entity",
  "sense_mode_entity",
  "cal_dry_value_entity",
  "cal_wet_value_entity",
  "cal_dry_set_entity",
  "cal_wet_set_entity",
  "fw_version_entity",
  "ota_target_version_entity",
  "ota_last_success_ts_entity",
];

// Expected unique_id suffixes (or contains) for each logical key.
const UNIQUE_ID_SUFFIX_MAP = {
  status_entity: ["_status", "_availability", "_online"],
  probe_entity: ["_probe_connected"],
  percent_entity: ["_percent"],
  liters_entity: ["_liters"],
  cm_entity: ["_centimeters", "_cm"],
  raw_entity: ["_raw"],
  percent_valid_entity: ["_percent_valid", "_percent_valid_bool"],
  calibration_entity: ["_calibration_state", "_calibration"],
  quality_reason_entity: ["_quality"],
  tank_volume_entity: ["_tank_volume_l"],
  rod_length_entity: ["_rod_length_cm"],
  calibrate_dry_entity: ["_calibrate_dry"],
  calibrate_wet_entity: ["_calibrate_wet"],
  clear_calibration_entity: ["_clear_calibration"],
  wipe_wifi_entity: ["_wipe_wifi"],
  ota_pull_entity: ["_ota_pull"],
  ota_state_entity: ["_ota_state"],
  ota_progress_entity: ["_ota_progress"],
  ota_error_entity: ["_ota_error", "_ota_last_error"],
  ota_last_status_entity: ["_ota_last_status"],
  ota_last_message_entity: ["_ota_last_message"],
  update_available_entity: ["_update_available"],
  simulation_mode_entity: ["_simulation_mode"],
  sense_mode_entity: ["_sense_mode"],
  cal_dry_value_entity: ["_cal_dry"],
  cal_wet_value_entity: ["_cal_wet"],
  cal_dry_set_entity: ["_cal_dry_set"],
  cal_wet_set_entity: ["_cal_wet_set"],
  fw_version_entity: ["_fw_version"],
  ota_target_version_entity: ["_ota_target_version"],
  ota_last_success_ts_entity: ["_ota_last_success_ts"],
};

// Matching rules per logical key (domains and entity_id fallbacks).
const ENTITY_MATCH_RULES = {
  status_entity: { domains: ["binary_sensor", "sensor"], entitySuffixes: ["_status", "_availability", "_online"] },
  probe_entity: { domains: ["binary_sensor"], entitySuffixes: ["_probe_connected", "_probe"] },
  percent_entity: { domains: ["sensor"], entitySuffixes: ["_percent"] },
  liters_entity: { domains: ["sensor"], entitySuffixes: ["_liters"] },
  cm_entity: { domains: ["sensor"], entitySuffixes: ["_centimeters", "_cm"] },
  raw_entity: { domains: ["sensor"], entitySuffixes: ["_raw"] },
  percent_valid_entity: { domains: ["binary_sensor", "sensor"], entitySuffixes: ["_percent_valid", "_percent_valid_bool"] },
  calibration_entity: { domains: ["sensor"], entitySuffixes: ["_calibration_state", "_calibration"] },
  quality_reason_entity: { domains: ["sensor"], entitySuffixes: ["_quality"] },
  tank_volume_entity: { domains: ["number", "input_number", "sensor"], entitySuffixes: ["_tank_volume_l", "_tank_volume"] },
  rod_length_entity: { domains: ["number", "input_number", "sensor"], entitySuffixes: ["_rod_length_cm", "_rod_length"] },
  calibrate_dry_entity: { domains: ["button"], entitySuffixes: ["_calibrate_dry"] },
  calibrate_wet_entity: { domains: ["button"], entitySuffixes: ["_calibrate_wet"] },
  clear_calibration_entity: { domains: ["button"], entitySuffixes: ["_clear_calibration"] },
  wipe_wifi_entity: { domains: ["button"], entitySuffixes: ["_wipe_wifi"] },
  ota_pull_entity: { domains: ["button"], entitySuffixes: ["_ota_pull"] },
  ota_state_entity: { domains: ["sensor"], entitySuffixes: ["_ota_state"] },
  ota_progress_entity: { domains: ["sensor"], entitySuffixes: ["_ota_progress"] },
  ota_error_entity: { domains: ["sensor"], entitySuffixes: ["_ota_error", "_ota_last_error"] },
  ota_last_status_entity: { domains: ["sensor"], entitySuffixes: ["_ota_last_status"] },
  ota_last_message_entity: { domains: ["sensor"], entitySuffixes: ["_ota_last_message"] },
  update_available_entity: { domains: ["binary_sensor"], entitySuffixes: ["_update_available"] },
  simulation_mode_entity: { domains: ["select", "input_select"], entitySuffixes: ["_simulation_mode"] },
  sense_mode_entity: { domains: ["select", "input_select"], entitySuffixes: ["_sense_mode"] },
  cal_dry_value_entity: { domains: ["sensor", "number", "input_number"], entitySuffixes: ["_cal_dry"] },
  cal_wet_value_entity: { domains: ["sensor", "number", "input_number"], entitySuffixes: ["_cal_wet"] },
  cal_dry_set_entity: { domains: ["number", "input_number"], entitySuffixes: ["_cal_dry_set"] },
  cal_wet_set_entity: { domains: ["number", "input_number"], entitySuffixes: ["_cal_wet_set"] },
  fw_version_entity: { domains: ["sensor"], entitySuffixes: ["_fw_version"] },
  ota_target_version_entity: { domains: ["sensor"], entitySuffixes: ["_ota_target_version"] },
  ota_last_success_ts_entity: { domains: ["sensor"], entitySuffixes: ["_ota_last_success_ts"] },
};

// Cache resolved configs per device_id to avoid repeated websocket queries.
const DEVICE_RESOLUTION_CACHE = new Map();

const normalizeStr = (v) => (v === null || v === undefined ? "" : String(v)).toLowerCase();
const matchesSuffix = (value, suffixes = []) => suffixes.some((suf) => value.endsWith(suf));
const domainOf = (entityId) => normalizeStr(entityId).split(".")[0];

const pickEntityIdForKey = (entries, key) => {
  const rules = ENTITY_MATCH_RULES[key] || {};
  const uniqueSuffixes = UNIQUE_ID_SUFFIX_MAP[key] || [];
  const entitySuffixes = rules.entitySuffixes || uniqueSuffixes;
  const domains = rules.domains;

  const inDomain = (entry) => !domains || domains.includes(domainOf(entry.entity_id));
  const scoped = entries.filter(inDomain);

  const byUnique = scoped.find((e) => matchesSuffix(normalizeStr(e.unique_id), uniqueSuffixes));
  if (byUnique) return byUnique.entity_id;

  const byEntityId = scoped.find((e) => matchesSuffix(normalizeStr(e.entity_id), entitySuffixes));
  if (byEntityId) return byEntityId.entity_id;

  return null;
};

const buildResolvedConfigFromRegistry = (deviceId, registryEntries) => {
  const scoped = (registryEntries || []).filter((e) => e && e.device_id === deviceId);
  const keys = [...REQUIRED_ENTITY_KEYS, ...OPTIONAL_ENTITY_KEYS];
  const resolved = {};
  const missing = [];

  keys.forEach((key) => {
    const entityId = pickEntityIdForKey(scoped, key);
    if (entityId) {
      resolved[key] = entityId;
    }
    else if (REQUIRED_ENTITY_KEYS.includes(key)) {
      missing.push(key);
    }
  });

  // Fallback: if no dedicated status entity found, mirror probe or percent_valid so the card can still render.
  if (!resolved.status_entity) {
    resolved.status_entity = resolved.probe_entity || resolved.percent_valid_entity || null;
    if (resolved.status_entity) {
      const idx = missing.indexOf("status_entity");
      if (idx >= 0) missing.splice(idx, 1);
    }
  }

  return { resolved, missing };
};

class WaterTankCard extends HTMLElement {
  constructor() {
    super();
    this.attachShadow({ mode: "open" });
    this._config = null;
    this._configInput = null;
    this._resolutionState = { status: "idle", error: null };
    this._resolutionRequestId = 0;
    this._warnedResolution = false;
    this._hass = null;
    this._modalOpen = false;
    this._modalPage = "main";
    this._focusAfterRender = null;
    this._toast = { open: false, text: "", type: "info" };
    this._toastTimer = null;
    this._pendingSet = {};
    this._activeEditKey = null;
    this._modalPointerHandler = null;
    this._suspendRender = false;
    this._deferredRender = false;
    this._deferTimer = null;
    this._scrollToOta = false;
    this._renderDeferMs = 350;
    this._lastInteractionAt = 0;
    this._otaUi = {
      active: false,
      startedAt: 0,
      lastSeenAt: 0,
      lastState: null,
      lastProgress: null,
      lastMessage: null,
      result: null,
    };
    this._draft = {
      tankVolume: "",
      rodLength: "",
      dryValue: "",
      wetValue: "",
      simMode: "",
    };
    this._editing = {
      dryValue: false,
      wetValue: false,
      tankVolume: false,
      rodLength: false,
      simMode: false,
    };
  }

  setConfig(config) {
    const defaults = {
      title: "Water Tank",
      view: "tank",
      device_id: null,
      // modal entities (optional)
      tank_volume_entity: null,
      rod_length_entity: null,
      calibrate_dry_entity: null,
      calibrate_wet_entity: null,
      clear_calibration_entity: null,
      wipe_wifi_entity: null,
      ota_pull_entity: null,
      ota_state_entity: null,
      ota_progress_entity: null,
      ota_error_entity: null,
      ota_last_status_entity: null,
      ota_last_message_entity: null,
      update_available_entity: null,
      fw_version_entity: null,
      ota_target_version_entity: null,

      // simulation controls (optional)
      simulation_mode_entity: null,
      sense_mode_entity: null,

      // optional calibration value readouts
      cal_dry_value_entity: null,
      cal_wet_value_entity: null,
      cal_dry_set_entity: null,
      cal_wet_set_entity: null,

      // optional diagnostics
      quality_reason_entity: null,
      ota_last_success_ts_entity: null,
    };

    const cfg = { ...defaults, ...config };
    this._configInput = cfg;
    this._warnedResolution = false;

    const hasAllEntities = REQUIRED_ENTITY_KEYS.every((k) => !!cfg[k]);

    if (!cfg.device_id && !hasAllEntities) {
      throw new Error(`Provide either device_id or explicit entity ids for: ${REQUIRED_ENTITY_KEYS.join(", ")}`);
    }

    // If user supplied everything, skip auto-resolve entirely.
    if (hasAllEntities) {
      this._config = cfg;
      this._resolutionState = { status: "done", error: null };
      this._render();
      return;
    }

    // Seed config with user-provided values; fill missing after resolution.
    this._config = cfg;
    this._resolutionState = { status: "pending", error: null };
    this._render();
    this._maybeResolveWithHass();
  }

  set hass(hass) {
    this._hass = hass;
    this._maybeResolveWithHass();
    this._checkPendingSets();
    if (this._shouldDeferRender()) {
      this._deferredRender = true;
      this._scheduleDeferredRender();
      return;
    }
    this._deferredRender = false;
    this._render();
  }

  getCardSize() {
    return 3;
  }

  _isFullyConfigured() {
    return this._config && REQUIRED_ENTITY_KEYS.every((k) => !!this._config[k]);
  }

  _maybeResolveWithHass() {
    if (!this._hass || !this._configInput?.device_id) return;
    if (!this._hass.connection || typeof this._hass.connection.sendMessagePromise !== "function") return;
    if (this._isFullyConfigured()) return;

    const deviceId = this._configInput.device_id;
    const cached = DEVICE_RESOLUTION_CACHE.get(deviceId);
    if (cached) {
      this._applyResolvedConfig(cached.resolved || {}, cached.missing || [], cached.error || null);
      return;
    }

    const reqId = ++this._resolutionRequestId;
    this._resolutionState = { status: "loading", error: null };
    this._render();

    this._hass.connection
      .sendMessagePromise({ type: "config/entity_registry/list" })
      .then((entries) => {
        if (this._resolutionRequestId !== reqId) return;
        const { resolved, missing } = buildResolvedConfigFromRegistry(deviceId, entries);
        DEVICE_RESOLUTION_CACHE.set(deviceId, { resolved, missing, error: null });
        this._applyResolvedConfig(resolved, missing, null);
      })
      .catch((err) => {
        if (this._resolutionRequestId !== reqId) return;
        const error = err?.message || "Failed to load entity registry";
        DEVICE_RESOLUTION_CACHE.set(deviceId, { resolved: {}, missing: REQUIRED_ENTITY_KEYS, error });
        this._applyResolvedConfig({}, REQUIRED_ENTITY_KEYS, error);
      });
  }

  _applyResolvedConfig(resolved, missing = [], error = null) {
    const merged = { ...this._configInput };
    [...REQUIRED_ENTITY_KEYS, ...OPTIONAL_ENTITY_KEYS].forEach((key) => {
      if (!merged[key] && resolved[key]) {
        merged[key] = resolved[key];
      }
    });

    if (this._hass?.states) {
      const entries = Object.values(this._hass.states);
      const pickBy = (domain, uniqSuffix, entitySuffix) => {
        const matchDomain = (e) => String(e?.entity_id || "").startsWith(domain + ".");
        const inDomain = entries.filter(matchDomain);
        const byUnique = inDomain.find((e) => String(e?.attributes?.unique_id || "").endsWith(uniqSuffix));
        if (byUnique) return byUnique.entity_id;
        const byEntity = inDomain.find((e) => String(e?.entity_id || "").endsWith(entitySuffix));
        return byEntity ? byEntity.entity_id : null;
      };

      if (!merged.sense_mode_entity) {
        merged.sense_mode_entity = pickBy("select", "_sense_mode", "_sense_mode");
      }
      if (!merged.simulation_mode_entity) {
        merged.simulation_mode_entity = pickBy("select", "_simulation_mode", "_simulation_mode");
      }

      if (merged.sense_mode_entity || merged.simulation_mode_entity) {
        console.log("[WT] auto-resolve", {
          sense_mode_entity: merged.sense_mode_entity || null,
          simulation_mode_entity: merged.simulation_mode_entity || null,
        });
      }
    }

    this._config = merged;

    if (error || (missing && missing.length)) {
      this._resolutionState = {
        status: error ? "error" : "incomplete",
        error: error || `Missing entities: ${missing.join(", ")}`,
      };
    }
    else {
      this._resolutionState = { status: "done", error: null };
    }

    this._render();
  }

  _renderErrorCard(title, message) {
    if (!this.shadowRoot) return;
    const safeTitle = this._safeText(title || "Water Tank");
    const safeMsg = this._safeText(message || "Missing configuration");
    this.shadowRoot.innerHTML = `
      <style>
        :host { display:block; }
        ha-card { padding: 16px; border-radius: 16px; }
        .err-title { font-size: 16px; font-weight: 800; margin-bottom: 6px; }
        .err-body { font-size: 14px; opacity: 0.8; }
      </style>
      <ha-card>
        <div class="err-title">${safeTitle}</div>
        <div class="err-body">${safeMsg}</div>
      </ha-card>`;
  }

  _renderLoadingCard(title, message) {
    if (!this.shadowRoot) return;
    const safeTitle = this._safeText(title || "Water Tank");
    const safeMsg = this._safeText(message || "Resolving device entities…");
    this.shadowRoot.innerHTML = `
      <style>
        :host { display:block; }
        ha-card { padding: 16px; border-radius: 16px; }
        .load-title { font-size: 16px; font-weight: 800; margin-bottom: 6px; }
        .load-body { font-size: 14px; opacity: 0.8; }
      </style>
      <ha-card>
        <div class="load-title">${safeTitle}</div>
        <div class="load-body">${safeMsg}</div>
      </ha-card>`;
  }

  // ---------- helpers ----------
  _state(entityId) {
    return this._hass?.states?.[entityId]?.state;
  }

  _num(entityId) {
    const v = parseFloat(this._state(entityId));
    return Number.isFinite(v) ? v : null;
  }

  _isOn(entityId) {
    return this._state(entityId) === "on";
  }

  _isOff(entityId) {
    return this._state(entityId) === "off";
  }

  _senseModeIsSimValue(value) {
    if (this._isUnknownState(value)) return null;
    const v = String(value).toLowerCase();
    if (v === "sim" || v === "simulation" || v === "1" || v === "true" || v === "on") return true;
    if (v === "touch" || v === "probe" || v === "0" || v === "false" || v === "off") return false;
    return null;
  }

  _senseModeIsSim(entityId) {
    const s = this._state(entityId);
    return this._senseModeIsSimValue(s);
  }

  _senseModeUiValue() {
    const pending = this._pendingSet?.senseMode;
    if (pending && pending.targetValue !== undefined && pending.targetValue !== null) return pending.targetValue;
    return this._state(this._config?.sense_mode_entity);
  }

  _labelForKey(key) {
    switch (key) {
      case "senseMode":
        return "Sense mode";
      case "simMode":
        return "Simulation mode";
      case "tankVolume":
        return "Tank volume";
      case "rodLength":
        return "Rod length";
      case "dryValue":
        return "Dry value";
      case "wetValue":
        return "Wet value";
      default:
        return "Value";
    }
  }

  _humanizeSenseMode(value) {
    const isSim = this._senseModeIsSimValue(value);
    if (isSim === true) return "Sim";
    if (isSim === false) return "Touch";
    return this._safeText(value, "");
  }

  _successMessageForPending(key, pending) {
    const label = pending?.label || this._labelForKey(key);
    if (key === "senseMode") return `Sense mode set to ${this._humanizeSenseMode(pending?.targetValue)}`;
    if (key === "simMode") return `Simulation mode set to ${this._safeText(pending?.targetValue, "")}`;
    return `Saved ${label}`;
  }

  _failureMessageForPending(key, pending, reason = "no update from device") {
    const label = pending?.label || this._labelForKey(key);
    const action = key === "senseMode" || key === "simMode" ? "set" : "save";
    return `Failed to ${action} ${label} (${reason})`;
  }

  _setPendingSenseMode(targetValue) {
    const entityId = this._config?.sense_mode_entity;
    if (!entityId) return false;
    const startedAt = Date.now();
    this._pendingSet.senseMode = {
      entityId,
      verifyEntityId: entityId,
      targetValue,
      startedAt,
      label: this._labelForKey("senseMode"),
    };
    this._showToast(`Setting sense mode to ${this._humanizeSenseMode(targetValue)}…`, "info", 0, { sticky: true });
    this._render();
    setTimeout(() => {
      if (this._pendingSet?.senseMode?.startedAt === startedAt) {
        this._checkPendingSets();
        if (this._modalOpen) this._render();
      }
    }, 5200);
    return true;
  }

  _isTruthyState(state) {
    if (state === true) return true;
    if (state === false || state === null || state === undefined) return false;
    const s = String(state).trim().toLowerCase();
    return s === "on" || s === "true" || s === "1";
  }

  _isOnlineState(state) {
    if (this._isUnknownState(state)) return false;
    if (state === true) return true;

    const s = String(state).trim().toLowerCase();

    // HA binary_sensor values
    if (s === "on") return true;
    if (s === "off") return false;

    // Some setups expose strings
    if (s === "online" || s === "connected" || s === "available") return true;
    if (s === "offline" || s === "disconnected") return false;

    // Fallback: treat truthy numeric/string as online
    return this._isTruthyState(s);
  }

  _isUnknownState(s) {
    if (s === null || s === undefined) return true;
    const v = String(s).trim().toLowerCase();
    return v === "unknown" || v === "unavailable" || v === "none";
  }

  _isOtaBusyValue(value) {
    if (this._isUnknownState(value)) return false;
    const v = String(value).trim().toLowerCase();
    return v === "downloading" || v === "verifying" || v === "applying" || v === "rebooting";
  }

  _otaSessionIsStale(now = Date.now()) {
    const s = this._otaUi;
    if (!s || !s.active) return false;
    if (!Number.isFinite(s.lastSeenAt) || s.lastSeenAt <= 0) return false;
    return (now - s.lastSeenAt) > OTA_UI_STALE_MS;
  }

  _startOtaUiSession(now = Date.now()) {
    this._otaUi.active = true;
    this._otaUi.startedAt = now;
    this._otaUi.lastSeenAt = now;
    this._otaUi.lastState = "queued";
    this._otaUi.lastProgress = null;
    this._otaUi.lastMessage = "Starting update…";
    this._otaUi.result = null;
  }

  _updateOtaUiSession(otaStateRaw, otaProgressVal, otaLastMessageRaw, now = Date.now()) {
    const session = this._otaUi;
    if (!session) return;

    let stateLower = null;
    if (!this._isUnknownState(otaStateRaw)) {
      stateLower = String(otaStateRaw).trim().toLowerCase();
      session.lastState = stateLower;
      session.lastSeenAt = now;
    }

    if (Number.isFinite(otaProgressVal)) {
      session.lastProgress = otaProgressVal;
      session.lastSeenAt = now;
    }

    if (!this._isUnknownState(otaLastMessageRaw)) {
      session.lastMessage = String(otaLastMessageRaw);
      session.lastSeenAt = now;
    }

    if (!session.active) return;

    if (stateLower === "success" || stateLower === "failed") {
      session.active = false;
      session.result = stateLower;
      return;
    }

    if ((now - session.startedAt) > OTA_UI_TIMEOUT_MS) {
      session.active = false;
      session.result = "failed";
      session.lastState = "failed";
      session.lastMessage = "Timed out";
      session.lastSeenAt = now;
    }
  }

  _safeText(v, fallback = "—") {
    if (v === null || v === undefined) return fallback;
    if (typeof v === "number" && !Number.isFinite(v)) return fallback;
    const s = String(v);
    return s.length ? s : fallback;
  }

  _humanizeAgeFromEpochSeconds(ts) {
    const n = Number(ts);
    if (!Number.isFinite(n) || n <= 0) return "—";
    const nowMs = Date.now();
    const tsMs = n * 1000;
    if (!Number.isFinite(tsMs) || tsMs <= 0) return "—";
    const diffMs = nowMs - tsMs;
    if (diffMs < 0) return "—";
    const diffMin = diffMs / 60000;
    if (diffMin < 60) return "just now";
    const diffHours = Math.round(diffMin / 60);
    if (diffHours < 24) return `${diffHours} hours ago`;
    const diffDays = Math.round(diffHours / 24);
    return `${diffDays} days ago`;
  }

  _timestampStateToEpochSeconds(raw) {
    if (this._isUnknownState(raw)) return NaN;

    if (typeof raw === "number") {
      if (!Number.isFinite(raw) || raw <= 0) return NaN;
      return raw > 1e12 ? raw / 1000 : raw;
    }

    const s = String(raw).trim();
    if (!s.length) return NaN;

    const numeric = Number(s);
    if (Number.isFinite(numeric) && numeric > 0) {
      return numeric > 1e12 ? numeric / 1000 : numeric;
    }

    const parsedMs = Date.parse(s);
    if (!Number.isFinite(parsedMs) || parsedMs <= 0) return NaN;
    return parsedMs / 1000;
  }

  _formatNumber(value, decimals) {
    const n = typeof value === "number" ? value : Number(value);
    if (!Number.isFinite(n)) return "—";
    return n.toFixed(decimals);
  }

  _formatVolume(value) {
    const n = typeof value === "number" ? value : Number(value);
    if (!Number.isFinite(n)) return { text: "—", unit: "L" };
    const abs = Math.abs(n);
    if (abs < 1) return { text: this._formatNumber(n * 1000, 0), unit: "mL" };
    if (abs > 100) return { text: this._formatNumber(n / 1000, 2), unit: "kL" };
    return { text: this._formatNumber(n, CARD_LITERS_DECIMALS), unit: "L" };
  }

  _formatHeight(value) {
    const n = typeof value === "number" ? value : Number(value);
    if (!Number.isFinite(n)) return { text: "—", unit: "cm" };
    const abs = Math.abs(n);
    if (abs < 5) return { text: this._formatNumber(n * 10, 0), unit: "mm" };
    if (abs > 1000) return { text: this._formatNumber(n / 100, 2), unit: "m" };
    return { text: this._formatNumber(n, CARD_HEIGHT_DECIMALS), unit: "cm" };
  }

  _boolLabelFromState(state, trueLabel = "true", falseLabel = "false") {
    if (this._isUnknownState(state)) return "Unknown";
    return this._isTruthyState(state) ? trueLabel : falseLabel;
  }

  _probeStateLabel(state) {
    if (this._isUnknownState(state)) return "Unknown";
    const s = String(state).trim().toLowerCase();
    if (s === "on") return "Connected";
    if (s === "off") return "Disconnected";
    return this._safeText(state);
  }

  _clamp(n, min, max) {
    return Math.max(min, Math.min(max, n));
  }

  _domain(entityId) {
    return entityId ? String(entityId).split(".")[0] : "";
  }

  _callService(domain, service, data = {}) {
    if (!this._hass || !domain || !service) return;
    try {
      this._hass.callService(domain, service, data);
    } catch (err) {
      // keep silent but avoid crash
      // eslint-disable-next-line no-console
      console.warn(
        `${CARD_TAG}: service call failed for ${domain}.${service}`,
        {
          entity: data && data.entity_id,
          error: (err && err.message) || err,
        }
      );
    }
  }

  _showToast(text, type = "info", ms = TOAST_RESULT_MS, opts = {}) {
    if (!text) return;
    this._toast = { open: true, text, type };
    if (this._toastTimer) clearTimeout(this._toastTimer);
    if (!opts?.sticky && ms > 0) {
      this._toastTimer = setTimeout(() => {
        this._toast.open = false;
        this._render();
      }, ms);
    }
    this._render();
  }

  _valuesMatch(current, target) {
    if (this._isUnknownState(current)) return false;
    if (this._isNumberLike(current) && this._isNumberLike(target)) {
      return Math.abs(Number(current) - Number(target)) < 0.001;
    }
    return String(current) === String(target);
  }

  _hasPendingSets() {
    return !!(this._pendingSet && Object.keys(this._pendingSet).length);
  }

  _markInteraction() {
    this._lastInteractionAt = Date.now();
  }

  _shouldDeferRender() {
    if (!this._modalOpen) return false;
    if (this._suspendRender) return true;
    const delta = Date.now() - (this._lastInteractionAt || 0);
    return delta >= 0 && delta < this._renderDeferMs;
  }

  _scheduleDeferredRender() {
    if (this._deferTimer) return;
    this._deferTimer = setTimeout(() => {
      this._deferTimer = null;
      if (!this._deferredRender) return;
      if (this._shouldDeferRender()) {
        this._scheduleDeferredRender();
        return;
      }
      this._deferredRender = false;
      this._render();
    }, this._renderDeferMs);
  }

  _checkPendingSets() {
    if (!this._pendingSet) return;
    const keys = Object.keys(this._pendingSet);
    if (!keys.length) return;
    const now = Date.now();
    keys.forEach((key) => {
      const pending = this._pendingSet[key];
      if (!pending) return;
      const current = this._state(pending.verifyEntityId || pending.entityId);
      const matches = this._valuesMatch(current, pending.targetValue);
      const expired = now - pending.startedAt > 5000;
      if (matches) {
        delete this._pendingSet[key];
        this._editing[key] = false;
        if (this._activeEditKey === key) this._activeEditKey = null;
        if (!this._isUnknownState(current)) this._draft[key] = current;
        let toastText = this._successMessageForPending(key, pending);
        let toastType = "success";

        if ((key === "dryValue" || key === "wetValue")) {
          const appliedEntity =
            key === "dryValue" ? this._config.cal_dry_value_entity : this._config.cal_wet_value_entity;

          if (appliedEntity && pending.entityId && appliedEntity !== pending.entityId) {
            const appliedNow = this._state(appliedEntity);
            if (!this._valuesMatch(appliedNow, pending.targetValue)) {
              toastText = `${this._successMessageForPending(key, pending)} (awaiting apply)`;
              toastType = "warn";
            }
          }
        }

        if (this._modalOpen) this._showToast(toastText, toastType, TOAST_RESULT_MS);
      }
      else if (expired) {
        const setNow = this._state(pending.entityId);
        const setOk = this._valuesMatch(setNow, pending.targetValue);

        delete this._pendingSet[key];

        // close edit either way
        this._editing[key] = false;
        if (this._activeEditKey === key) this._activeEditKey = null;

        if (this._modalOpen) {
          if (setOk) {
            // We successfully wrote the setpoint, but the applied sensor (if configured)
            // may still be catching up.
            let toastText = this._successMessageForPending(key, pending);
            let toastType = "success";

            if (key === "dryValue" || key === "wetValue") {
              const appliedEntity =
                key === "dryValue" ? this._config.cal_dry_value_entity : this._config.cal_wet_value_entity;

              if (appliedEntity) {
                const appliedNow = this._state(appliedEntity);
                if (!this._valuesMatch(appliedNow, pending.targetValue)) {
                  toastText = `${this._successMessageForPending(key, pending)} (awaiting apply)`;
                  toastType = "warn";
                }
              }
            }

            this._showToast(toastText, toastType, TOAST_RESULT_MS);
          } else {
            this._showToast(this._failureMessageForPending(key, pending), "error", TOAST_RESULT_MS);
          }
        }
      }
    });

    if (this._deferredRender && !this._shouldDeferRender()) {
      this._deferredRender = false;
      this._render();
    }
  }

  _setNumber(entityId, value, label = "value") {
    if (!entityId || value === undefined || !this._hass) return false;
    const domain = this._domain(entityId);
    const payload = { entity_id: entityId, value: Number(value) };
    try {
      if (domain === "number") {
        this._hass.callService("number", "set_value", payload);
        return true;
      }
      if (domain === "input_number") {
        this._hass.callService("input_number", "set_value", payload);
        return true;
      }
    } catch (err) {
      this._showToast(`Failed to save ${label} (service unavailable)`, "error", TOAST_RESULT_MS);
    }
    return false;
  }

  _getCalSetEntityId(draftKey) {
    if (draftKey === "dryValue") {
      return this._config.cal_dry_set_entity || this._config.cal_dry_value_entity;
    }
    if (draftKey === "wetValue") {
      return this._config.cal_wet_set_entity || this._config.cal_wet_value_entity;
    }
    return null;
  }

  _commitCalEdit(draftKey) {
    if (!draftKey || this._pendingSet?.[draftKey]) return false;
    const label = this._labelForKey(draftKey);
    const entityId = this._getCalSetEntityId(draftKey);
    if (!entityId) return false;
    const domain = this._domain(entityId);
    if (domain !== "number" && domain !== "input_number") return false;
    const raw = this._draft[draftKey];
    const trimmed = raw !== null && raw !== undefined ? String(raw).trim() : "";
    if (!trimmed || !this._isNumberLike(trimmed)) {
      this._showToast(`${label} must be a number`, "warn", TOAST_RESULT_MS);
      return false;
    }
    const numVal = Number(trimmed);
    if (!this._setNumber(entityId, numVal, label)) return false;

    const startedAt = Date.now();

    const appliedEntityId =
      draftKey === "dryValue" ? this._config.cal_dry_value_entity :
        draftKey === "wetValue" ? this._config.cal_wet_value_entity :
          null;

    // Always verify the write against the entity we actually set (input_number/number).
    // The separate “applied” sensor can lag behind; we’ll surface that as "awaiting apply"
    // without treating it as a failed save.
    const verifyEntityId = entityId;

    this._pendingSet[draftKey] = {
      entityId,          // where we wrote
      verifyEntityId,    // what we consider “saved successfully”
      targetValue: numVal,
      startedAt,
      label,
    };

    this._showToast(`Saving ${label}…`, "info", 0, { sticky: true });
    setTimeout(() => {
      const p = this._pendingSet?.[draftKey];
      if (!p || p.startedAt !== startedAt) return;

      this._checkPendingSets();

      // if still pending after check, force a failure toast + cleanup
      if (this._pendingSet?.[draftKey]?.startedAt === startedAt) {
        delete this._pendingSet[draftKey];
        this._editing[draftKey] = false;
        if (this._activeEditKey === draftKey) this._activeEditKey = null;
        this._showToast(this._failureMessageForPending(draftKey, { label }, "no update from device"), "error", TOAST_RESULT_MS);
      }

      if (this._modalOpen) this._render();
    }, 5200);
    return true;
  }

  _commitNumberEdit(draftKey, entityId, rawValue, opts = {}) {
    if (!draftKey || !entityId || !this._hass) return false;
    if (this._pendingSet?.[draftKey]) return false;

    const label = opts.label || this._labelForKey(draftKey);

    const trimmed = rawValue !== null && rawValue !== undefined ? String(rawValue).trim() : "";
    if (!trimmed || !this._isNumberLike(trimmed)) {
      this._showToast(`${label} must be a number`, "warn", TOAST_RESULT_MS);
      return false;
    }

    const numVal = Number(trimmed);
    if (!(numVal > 0)) {
      this._showToast(`${label} must be greater than 0`, "warn", TOAST_RESULT_MS);
      return false;
    }

    const current = this._num(entityId);
    const unchanged = current !== null && Math.abs(current - numVal) < 0.001;
    if (unchanged) return false;

    if (!this._setNumber(entityId, numVal, label)) {
      return false;
    }

    const startedAt = Date.now();
    this._pendingSet[draftKey] = { entityId, targetValue: numVal, startedAt, label };
    this._showToast(`Saving ${label}…`, "info", 0, { sticky: true });

    setTimeout(() => {
      if (this._pendingSet?.[draftKey]?.startedAt === startedAt) {
        this._checkPendingSets();
        if (this._modalOpen) this._render();
      }
    }, 5200);

    return true;
  }

  _cancelCalEdit(draftKey) {
    if (!draftKey || this._pendingSet?.[draftKey]) return;
    const current = this._getCalDisplayValue(draftKey);
    if (!this._isUnknownState(current)) this._draft[draftKey] = current;
    this._editing[draftKey] = false;
    if (this._activeEditKey === draftKey) this._activeEditKey = null;
    this._showToast(`Canceled ${this._labelForKey(draftKey)} edit`, "info", TOAST_RESULT_MS);
  }

  _getOptions(entityId) {
    if (!entityId || !this._hass?.states?.[entityId]) return [];
    return this._hass.states[entityId].attributes?.options || [];
  }

  _isNumberLike(str) {
    if (str === null || str === undefined) return false;
    const n = Number(str);
    return Number.isFinite(n);
  }

  _buildOnlineBadgeHtml(online) {
    const bg = online ? "rgba(0,150,0,0.12)" : "rgba(255,0,0,0.12)";
    const br = online ? "rgba(0,150,0,0.35)" : "rgba(255,0,0,0.35)";
    return `<span class="wt-badge" style="background:${bg};border-color:${br};">${online ? "Online" : "Offline"}</span>`;
  }

  _qualityMeta(reason) {
    if (this._isUnknownState(reason)) return null;
    const key = String(reason || "").toLowerCase();
    if (!key || key === "ok") return null;

    const map = {
      disconnected_low_raw: { label: "Probe reading too low (likely unplugged)", icon: "mdi:power-plug-off-outline", severity: "error", display: "nullify" },
      unreliable_spikes: { label: "Signal spiking (noise)", icon: "mdi:waveform", severity: "warn" },
      unreliable_rapid_fluctuation: { label: "Rapid fluctuation", icon: "mdi:chart-bell-curve", severity: "warn" },
      unreliable_stuck: { label: "Signal stuck (flatline)", icon: "mdi:sine-wave", severity: "warn" },
      out_of_bounds: { label: "Raw reading out of bounds", icon: "mdi:ruler", severity: "error", display: "dim" },
      calibration_recommended: { label: "Recalibrate: raw drifted past saved range", icon: "mdi:target-account", severity: "warn", display: "normal" },
      zero_hits: { label: "Zero readings seen (check wiring)", icon: "mdi:equal", severity: "warn" },
      unknown: { label: "Quality unknown", icon: "mdi:help-circle-outline", severity: "warn" },
    };

    return map[key] || { label: `Quality issue (${key})`, icon: "mdi:waveform", severity: "warn" };
  }

  _collectWarnings() {
    const statusState = this._state(this._config.status_entity);
    const probeState = this._state(this._config.probe_entity);
    const calState = this._state(this._config.calibration_entity);
    const percentValidState = this._state(this._config.percent_valid_entity);

    const warnings = [];
    if (!this._isOnlineState(statusState)) warnings.push({ icon: "mdi:lan-disconnect", text: "Device offline or status unknown" });
    if (probeState === "off") warnings.push({ icon: "mdi:power-plug-off-outline", text: "Probe disconnected" });
    else if (this._isUnknownState(probeState)) warnings.push({ icon: "mdi:help-circle-outline", text: "Probe state unknown" });

    if (calState === "needs_calibration") warnings.push({ icon: "mdi:ruler", text: "Needs calibration" });
    else if (calState === "calibrating") warnings.push({ icon: "mdi:cog", text: "Calibrating…" });

    if (!this._isTruthyState(percentValidState)) warnings.push({ icon: "mdi:alert-outline", text: "Readings not valid" });

    if (this._config.quality_reason_entity) {
      const qr = this._state(this._config.quality_reason_entity);
      const qm = this._qualityMeta(qr);
      if (qm) {
        warnings.push({ icon: qm.icon, text: qm.label });
      }
    }

    return warnings;
  }

  _getCalDisplayValue(draftKey) {
    const appliedEntityId = draftKey === "dryValue"
      ? this._config.cal_dry_value_entity
      : draftKey === "wetValue"
        ? this._config.cal_wet_value_entity
        : null;
    const setEntityId = this._getCalSetEntityId(draftKey);
    const appliedVal = appliedEntityId ? this._state(appliedEntityId) : null;
    if (!this._isUnknownState(appliedVal)) return appliedVal;
    return setEntityId ? this._state(setEntityId) : null;
  }

  _syncDraftFromEntities(force = false) {
    const setDraft = (key, value) => {
      if (this._pendingSet?.[key]) return;
      if (!force) {
        if (this._editing?.[key]) return;
      }
      if (value === null || value === undefined) return;
      if (this._isUnknownState(value)) return;
      this._draft[key] = String(value);
    };

    setDraft("tankVolume", this._state(this._config.tank_volume_entity));
    setDraft("rodLength", this._state(this._config.rod_length_entity));
    setDraft("dryValue", this._getCalDisplayValue("dryValue"));
    setDraft("wetValue", this._getCalDisplayValue("wetValue"));
    setDraft("simMode", this._state(this._config.simulation_mode_entity));
  }

  _openModal(page = "main") {
    this._modalOpen = true;
    this._modalPage = page;
    this._syncDraftFromEntities(true);
    this._render();
  }

  _closeModal() {
    this._modalOpen = false;
    this._activeEditKey = null;
    this._suspendRender = false;
    this._deferredRender = false;
    if (this._deferTimer) {
      clearTimeout(this._deferTimer);
      this._deferTimer = null;
    }
    if (this._editing) {
      Object.keys(this._editing).forEach((key) => {
        this._editing[key] = false;
      });
    }
    this._render();
  }

  _goModal(page) {
    this._modalPage = page;
    this._render();
  }

  // ---------- state model ----------
  _computeUiState() {
    const status = this._state(this._config.status_entity);
    const probeState = this._state(this._config.probe_entity);
    const percentValidState = this._state(this._config.percent_valid_entity);
    const calState = this._state(this._config.calibration_entity);
    const qualityReason = this._config.quality_reason_entity ? this._state(this._config.quality_reason_entity) : null;

    const offline = !this._isOnlineState(status);
    if (offline) return { display: "nullify", severity: "error", icon: "mdi:lan-disconnect", msg: "Device offline" };

    if (probeState === "off") return { display: "nullify", severity: "error", icon: "mdi:power-plug-off-outline", msg: "Probe disconnected" };

    if (this._isUnknownState(probeState)) return { display: "nullify", severity: "warn", icon: "mdi:help-circle-outline", msg: "Probe status unknown" };

    if (calState === "calibrating") return { display: "dim", severity: "warn", icon: "mdi:cog", msg: "Calibrating…" };

    if (calState === "needs_calibration") return { display: "dim", severity: "warn", icon: "mdi:ruler", msg: "Needs calibration" };

    if (!this._isTruthyState(percentValidState)) return { display: "dim", severity: "warn", icon: "mdi:alert-outline", msg: "Readings not valid" };

    const qm = this._qualityMeta(qualityReason);
    if (qm) {
      const display = qm.display || (qm.severity === "error" ? "dim" : "normal");
      return { display, severity: qm.severity || "warn", icon: qm.icon || "mdi:waveform", msg: qm.label };
    }

    return { display: "normal", severity: "ok", icon: "mdi:check-circle-outline", msg: "All readings valid" };
  }

  // ---------- modal (in-card) ----------
  _renderModalHtml() {
    if (!this._modalOpen) return "";
    this._syncDraftFromEntities(false);

    const statusState = this._state(this._config.status_entity);
    const online = this._isOnlineState(statusState);
    const badge = this._buildOnlineBadgeHtml(online);
    const warnings = this._collectWarnings();

    const warningsHtml = warnings.length
      ? `<ul class="wt-warning-list">${warnings
        .map((w) => `<li><ha-icon icon="${w.icon}"></ha-icon><span>${this._safeText(w.text)}</span></li>`)
        .join("")}</ul>`
      : `<div class="wt-all-good">The device is operating normally.</div>`;

    const probeState = this._state(this._config.probe_entity);
    const probeDisconnected = probeState === "off";
    const probeUnknown = this._isUnknownState(probeState);
    const toastHtml = this._toast?.open && this._toast.text
      ? `<div class="wt-toast-float"><div class="wt-toast wt-toast--${this._toast.type}">${this._safeText(this._toast.text, "")}</div></div>`
      : "";

    const renderCalValue = (key, label, appliedEntityId, draftKey) => {
      const setEntityId = this._getCalSetEntityId(draftKey); // setpoint (input_number/number)
      const setDomain = this._domain(setEntityId);
      const editable = !!setEntityId && (setDomain === "number" || setDomain === "input_number");
      const isPending = !!this._pendingSet?.[draftKey];
      const isEditing = !!this._editing[draftKey];

      const setValRaw = setEntityId ? this._state(setEntityId) : null;
      const appliedValRaw = appliedEntityId ? this._state(appliedEntityId) : null;

      // ✅ Primary display = applied/current calibration value (fallback to setpoint if applied is unknown)
      const displayRaw = !this._isUnknownState(appliedValRaw) ? appliedValRaw : setValRaw;
      const displayVal = this._safeText(displayRaw);

      // ✅ Only show the applied helper while editing
      const appliedHelper =
        isEditing &&
          appliedEntityId &&
          !this._isUnknownState(appliedValRaw)
          ? `<div class="wt-cal-secondary">Applied: ${this._safeText(appliedValRaw)}</div>`
          : "";

      if (!editable || !isEditing) {
        return `
      <div class="wt-cal-value">
        <div class="wt-cal-label">${label}</div>
        <div class="wt-cal-input-row">
          <button class="wt-cal-edit" id="wt-${key}-edit" type="button" ${editable ? "" : "disabled"}>
            <span class="wt-cal-edit-value">${displayVal}</span>
            <span class="wt-cal-edit-hint">${editable ? (isPending ? "Saving…" : "Edit") : "Read-only"}</span>
          </button>
        </div>
      </div>`;
      }

      return `
    <div class="wt-cal-value">
      <div class="wt-cal-label">${label}</div>
      <div class="wt-cal-input-row">
        <input id="wt-${key}-input" type="text" value="${this._safeText(this._draft[draftKey], "")}" />
        <button class="wt-mini-btn" id="wt-${key}-set" ${editable && !isPending ? "" : "disabled"}>Set</button>
      </div>
      ${appliedHelper}
    </div>`;
    };

    const dryDisabled = probeDisconnected || !this._config.calibrate_dry_entity;
    const wetDisabled = probeDisconnected || !this._config.calibrate_wet_entity;
    const calibrationSection = `
      <div class="wt-section">
        <div class="wt-section-head">
          <div>
            <div class="wt-section-title">Calibration</div>
            <div class="wt-section-sub">Calibrate the probe by setting its dry and fully submerged values.</div>
          </div>
          ${this._config.clear_calibration_entity
        ? `<button class="wt-icon-btn" id="btnCalClear"><ha-icon icon="mdi:close-circle-outline"></ha-icon></button>`
        : ""}
        </div>

        <div class="wt-cal-row">
          <button class="wt-tile ${dryDisabled ? "disabled" : ""}" ${dryDisabled ? "disabled" : ""} id="btnCalDry" data-entity="${this._config.calibrate_dry_entity || ""}">
            <ha-icon icon="mdi:water-off-outline"></ha-icon>
            <span>Dry</span>
          </button>
          <button class="wt-tile ${wetDisabled ? "disabled" : ""}" ${wetDisabled ? "disabled" : ""} id="btnCalWet" data-entity="${this._config.calibrate_wet_entity || ""}">
            <ha-icon icon="mdi:water"></ha-icon>
            <span>Wet</span>
          </button>
        </div>
        ${probeDisconnected
        ? `<div class="wt-note">Probe disconnected — calibration disabled.</div>`
        : probeUnknown
          ? `<div class="wt-note">Probe state unknown; calibration may not work.</div>`
          : `<div class="wt-note">Dry: probe in air • Wet: fully submerged</div>`}
        <div class="wt-cal-values">
          ${renderCalValue("dry", "Dry value", this._config.cal_dry_value_entity, "dryValue")}
          ${renderCalValue("wet", "Wet value", this._config.cal_wet_value_entity, "wetValue")}
        </div>
      </div>`;

    const tankValue = this._editing.tankVolume ? this._draft.tankVolume : this._state(this._config.tank_volume_entity);
    const rodValue = this._editing.rodLength ? this._draft.rodLength : this._state(this._config.rod_length_entity);

    const tankRow = this._config.tank_volume_entity
      ? `
        <div class="wt-setup-row">
          <div class="wt-setup-icon"><ha-icon icon="mdi:water"></ha-icon></div>
          <div class="wt-setup-body">
            <div class="wt-setup-label">Tank volume</div>
            <div class="wt-setup-input">
              <input id="tankVolumeInput" type="text" placeholder="Litres" value="${this._safeText(tankValue, "")}" />
              <span class="wt-unit">L</span>
              <button class="wt-btn" id="tankVolumeSave" data-entity="${this._config.tank_volume_entity}">Save</button>
            </div>
            <div class="wt-error" id="tankVolumeError"></div>
            <div class="wt-setup-help">Used to calculate liters</div>
          </div>
        </div>`
      : "";

    const rodRow = this._config.rod_length_entity
      ? `
        <div class="wt-setup-row">
          <div class="wt-setup-icon"><ha-icon icon="mdi:ruler"></ha-icon></div>
          <div class="wt-setup-body">
            <div class="wt-setup-label">Rod length</div>
            <div class="wt-setup-input">
              <input id="rodLengthInput" type="text" placeholder="Centimeters" value="${this._safeText(rodValue, "")}" />
              <span class="wt-unit">cm</span>
              <button class="wt-btn" id="rodLengthSave" data-entity="${this._config.rod_length_entity}">Save</button>
            </div>
            <div class="wt-error" id="rodLengthError"></div>
            <div class="wt-setup-help">Used to calculate cm</div>
          </div>
        </div>`
      : "";

    const setupSection =
      this._config.tank_volume_entity || this._config.rod_length_entity
        ? `
      <div class="wt-section">
        <div class="wt-section-title">Setup</div>
        <div class="wt-setup">
          ${tankRow}
          ${rodRow}
        </div>
      </div>`
        : "";

    const mainPage = `
      <div class="wt-modal-header">
        <div class="wt-modal-title">Settings</div>
        <div class="wt-modal-actions">
          ${badge}
          <button class="wt-modal-close" aria-label="Close">✕</button>
        </div>
      </div>
      <div class="wt-warnings">${warningsHtml}</div>
      ${calibrationSection}
      ${setupSection}
      <div class="wt-modal-footer">
        <button class="wt-link wt-modal-advanced"><ha-icon icon="mdi:chevron-right"></ha-icon><span>Advanced</span></button>
      </div>
    `;

    const simEnabledState = this._config.sense_mode_entity
      ? this._senseModeIsSimValue(this._senseModeUiValue())
      : false;
    const simModeEntity = this._config.simulation_mode_entity;
    const simModeState = simModeEntity ? this._state(simModeEntity) : "";
    const simModePending = this._pendingSet?.simMode;
    const simModeSelected = simModePending
      ? simModePending.targetValue
      : (this._editing.simMode ? this._draft.simMode : simModeState);
    const simOptions = this._getOptions(simModeEntity);

    const diagnosticsLines = [
      { label: "Status", value: this._isOnlineState(statusState) ? "Online" : "Offline" },
      { label: "Probe", value: this._probeStateLabel(probeState) },
      { label: "Calibration", value: this._safeText(this._state(this._config.calibration_entity)) },
      { label: "Percent valid", value: this._boolLabelFromState(this._state(this._config.percent_valid_entity)) },
    ];
    if (this._config.quality_reason_entity) {
      const qualityReason = this._state(this._config.quality_reason_entity);
      const qm = this._qualityMeta(qualityReason);
      const qualityLabel = qm ? `${qm.label} (${qualityReason})` : qualityReason;
      diagnosticsLines.push({ label: "Quality", value: this._safeText(qualityLabel) });
    }
    if (this._config.ota_last_success_ts_entity) {
      const tsRaw = this._state(this._config.ota_last_success_ts_entity);
      const tsValue = this._timestampStateToEpochSeconds(tsRaw);
      diagnosticsLines.push({ label: "Last update", value: this._humanizeAgeFromEpochSeconds(tsValue) });
    }
    if (this._config.percent_entity) diagnosticsLines.push({ label: "Percent", value: this._formatNumber(this._num(this._config.percent_entity), DIAGNOSTICS_DECIMALS) });
    if (this._config.liters_entity) {
      const v = this._formatVolume(this._num(this._config.liters_entity));
      diagnosticsLines.push({ label: "Volume", value: `${v.text} ${v.unit}` });
    }
    if (this._config.cm_entity) {
      const h = this._formatHeight(this._num(this._config.cm_entity));
      diagnosticsLines.push({ label: "Height", value: `${h.text} ${h.unit}` });
    }
    if (this._config.raw_entity) diagnosticsLines.push({ label: "Raw", value: this._safeText(this._state(this._config.raw_entity)) });

    const diagnosticsHtml = diagnosticsLines
      .map((l) => `<div class="wt-diag-row"><span>${l.label}</span><b>${l.value}</b></div>`)
      .join("");

    const otaState = this._config.ota_state_entity ? this._state(this._config.ota_state_entity) : null;
    const otaBusy = this._isOtaBusyValue(otaState) || !!this._otaUi?.active;
    const otaProgressVal = this._config.ota_progress_entity ? this._num(this._config.ota_progress_entity) : null;
    const otaProgressEff = otaProgressVal === null ? this._otaUi?.lastProgress : otaProgressVal;
    const otaProgressLabel = otaProgressEff === null || otaProgressEff === undefined ? "—" : `${this._formatNumber(otaProgressEff, 0)}%`;
    const otaLastStatus = this._config.ota_last_status_entity ? this._state(this._config.ota_last_status_entity) : null;
    const otaLastMessageRaw = this._config.ota_last_message_entity ? this._state(this._config.ota_last_message_entity) : null;
    const otaLastMessage = this._isUnknownState(otaLastMessageRaw) ? this._otaUi?.lastMessage : otaLastMessageRaw;
    const updateAvailableState = this._config.update_available_entity ? this._state(this._config.update_available_entity) : null;
    const updateAvailableLabel = this._boolLabelFromState(updateAvailableState, "Yes", "No");
    const otaStateKnown = !this._isUnknownState(otaState);
    const otaStateDisplay = this._otaSessionIsStale(Date.now())
      ? "Rebooting / reconnecting…"
      : (otaStateKnown ? otaState : (this._otaUi?.lastState || otaState));
    const otaButtonDisabled = !online || otaBusy || !this._config.ota_pull_entity;
    const hasOtaSection =
      !!(this._config.ota_pull_entity ||
        this._config.ota_state_entity ||
        this._config.ota_progress_entity ||
        this._config.ota_last_status_entity ||
        this._config.ota_last_message_entity ||
        this._config.update_available_entity);
    const otaSection = hasOtaSection ? `
      <div class="wt-section" id="ota-section">
        <div class="wt-section-sub" style="margin-bottom:8px;">OTA Update</div>
        <div class="wt-diag-list">
          <div class="wt-diag-row"><span>Update available</span><b>${this._safeText(updateAvailableLabel)}</b></div>
          <div class="wt-diag-row"><span>State</span><b>${this._safeText(otaStateDisplay)}</b></div>
          <div class="wt-diag-row"><span>Progress</span><b>${otaProgressLabel}</b></div>
          <div class="wt-diag-row"><span>Last status</span><b>${this._safeText(otaLastStatus)}</b></div>
          <div class="wt-diag-row"><span>Last message</span><b>${this._safeText(otaLastMessage)}</b></div>
        </div>
        <div class="wt-setup-input" style="margin-top:10px;">
          <button class="wt-btn" id="btnOtaUpdate" data-entity="${this._config.ota_pull_entity || ""}" ${otaButtonDisabled ? "disabled" : ""}>
            Update now
          </button>
        </div>
      </div>` : "";

    const advancedPage = `
      <div class="wt-modal-header">
        <button class="wt-link wt-modal-back"><ha-icon icon="mdi:chevron-left"></ha-icon><span>Back</span></button>
        <div class="wt-modal-actions">
          ${badge}
          <button class="wt-modal-close" aria-label="Close">✕</button>
        </div>
      </div>
      <div class="wt-section">
        <div class="wt-section-title">Advanced</div>
      </div>
      ${(this._config.sense_mode_entity || this._config.simulation_mode_entity) ? `
      <div class="wt-section">
        <div class="wt-section-sub" style="margin-bottom:8px;">Simulation controls</div>
        ${this._config.sense_mode_entity
          ? `<div class="wt-setup-row">
            <div class="wt-setup-icon"><ha-icon icon="mdi:toggle-switch"></ha-icon></div>
            <div class="wt-setup-body">
              <div class="wt-setup-label">Sense mode</div>
              <div class="wt-setup-input">
                <button class="wt-toggle ${simEnabledState ? "on" : ""}" id="simToggleBtn" data-entity="${this._config.sense_mode_entity}" data-kind="select" data-state="${simEnabledState ? "on" : "off"}">
                  <span class="wt-toggle-thumb"></span>
                  <span class="wt-toggle-label">${simEnabledState ? "On" : "Off"}</span>
                </button>
              </div>
            </div>
          </div>`
          : ""}
        ${this._config.simulation_mode_entity
          ? `<div class="wt-setup-row">
            <div class="wt-setup-icon"><ha-icon icon="mdi:chart-line"></ha-icon></div>
            <div class="wt-setup-body">
              <div class="wt-setup-label">Simulation mode</div>
              <div class="wt-setup-input">
                <select id="simModeSelect" data-entity="${simModeEntity || ""}">
                  ${simOptions.map((opt) => `<option value="${opt}" ${String(opt) === String(simModeSelected) ? "selected" : ""}>${opt}</option>`).join("")}
                </select>
              </div>
            </div>
          </div>`
          : ""}
      </div>` : ""}

      <div class="wt-section">
        <div class="wt-section-sub" style="margin-bottom:8px;">Diagnostics</div>
        <div class="wt-diag-list">${diagnosticsHtml}</div>
      </div>
      ${otaSection}
      ${this._config.wipe_wifi_entity ? `
      <div class="wt-section">
        <div class="wt-section-sub" style="margin-bottom:8px;">Maintenance</div>
        <div class="wt-setup-row">
          <div class="wt-setup-icon"><ha-icon icon="mdi:wifi-remove"></ha-icon></div>
          <div class="wt-setup-body">
            <div class="wt-setup-label">Wipe WiFi credentials</div>
            <div class="wt-setup-help">Clears stored WiFi and reboots into setup mode.</div>
            <div class="wt-setup-input">
              <button class="wt-btn wt-btn-danger" id="btnWipeWifi" data-entity="${this._config.wipe_wifi_entity}">Wipe WiFi</button>
            </div>
          </div>
        </div>
      </div>
      ` : ""}
    `;

    const content = this._modalPage === "advanced" ? advancedPage : mainPage;

    return `
      <div class="wt-modal-overlay">
        <div class="wt-modal-card">
          ${content}
        </div>
        ${toastHtml}
      </div>
    `;
  }

  _bindModalHandlers() {
    if (!this.shadowRoot) return;

    const removeHandlers = () => {
      if (this._modalEscHandler) window.removeEventListener("keydown", this._modalEscHandler);
      if (this._modalPointerHandler) document.removeEventListener("pointerdown", this._modalPointerHandler, true);
    };

    if (!this._modalOpen) {
      removeHandlers();
      return;
    }

    if (!this._modalEscHandler) {
      this._modalEscHandler = (e) => {
        if (e.key === "Escape") {
          if (this._activeEditKey === "dryValue" || this._activeEditKey === "wetValue") {
            e.preventDefault();
            e.stopPropagation();
            this._cancelCalEdit(this._activeEditKey);
            return;
          }
          this._closeModal();
        }
      };
    }
    window.addEventListener("keydown", this._modalEscHandler);

    if (!this._modalPointerHandler) {
      this._modalPointerHandler = (e) => {
        if (!this._modalOpen || !this.shadowRoot) return;
        if (this._activeEditKey !== "dryValue" && this._activeEditKey !== "wetValue") return;
        if (this._pendingSet?.[this._activeEditKey]) return;
        const inputId = this._activeEditKey === "dryValue" ? "wt-dry-input" : "wt-wet-input";
        const input = this.shadowRoot.getElementById(inputId);
        if (!input) return;
        const row = input.closest(".wt-cal-input-row");
        const path = typeof e.composedPath === "function" ? e.composedPath() : [];
        if (row && path.includes(row)) return;
        this._commitCalEdit(this._activeEditKey);
      };
    }
    document.addEventListener("pointerdown", this._modalPointerHandler, true);

    const sr = this.shadowRoot;
    const overlay = sr.querySelector(".wt-modal-overlay");
    if (overlay) {
      overlay.addEventListener("click", (e) => {
        if (e.target === overlay) {
          e.stopPropagation();
          this._closeModal();
        }
      });
    }

    const modalCard = sr.querySelector(".wt-modal-card");
    if (modalCard) {
      if (!this._modalInteractionHandler) {
        this._modalInteractionHandler = () => this._markInteraction();
      }
      ["pointerdown", "keydown", "input", "change"].forEach((evt) => {
        modalCard.addEventListener(evt, this._modalInteractionHandler, true);
      });
    }

    const closeBtn = sr.querySelector(".wt-modal-close");
    if (closeBtn) closeBtn.onclick = (e) => {
      e.stopPropagation();
      this._closeModal();
    };

    const advLink = sr.querySelector(".wt-modal-advanced");
    if (advLink) advLink.onclick = (e) => {
      e.stopPropagation();
      this._goModal("advanced");
    };

    const backLink = sr.querySelector(".wt-modal-back");
    if (backLink) backLink.onclick = (e) => {
      e.stopPropagation();
      this._goModal("main");
    };

    const pressButton = (entityId) => {
      if (!entityId) return;
      this._callService("button", "press", { entity_id: entityId });
    };
    const captureCal = (label, entityId) => {
      const probeState = this._state(this._config.probe_entity);
      if (probeState === "off") {
        this._showToast("Calibration failed (probe disconnected)", "error", TOAST_RESULT_MS);
        return;
      }
      if (!entityId) return;
      this._showToast(`Capturing ${label.toLowerCase()} reference…`, "info", TOAST_RESULT_MS);
      pressButton(entityId);
    };

    // Calibration buttons
    const dryBtn = sr.getElementById("btnCalDry");
    if (dryBtn && !dryBtn.classList.contains("disabled")) {
      dryBtn.onclick = (e) => {
        e.preventDefault();
        e.stopPropagation();
        captureCal("Dry", dryBtn.dataset.entity);
      };
    }
    const wetBtn = sr.getElementById("btnCalWet");
    if (wetBtn && !wetBtn.classList.contains("disabled")) {
      wetBtn.onclick = (e) => {
        e.preventDefault();
        e.stopPropagation();
        captureCal("Wet", wetBtn.dataset.entity);
      };
    }
    const clearBtn = sr.getElementById("btnCalClear");
    if (clearBtn) {
      clearBtn.onclick = (e) => {
        e.preventDefault();
        e.stopPropagation();
        pressButton(this._config.clear_calibration_entity);
      };
    }
    const wipeWifiBtn = sr.getElementById("btnWipeWifi");
    if (wipeWifiBtn) {
      wipeWifiBtn.onclick = (e) => {
        e.preventDefault();
        e.stopPropagation();
        const ok = window.confirm("Wipe WiFi credentials and reboot into setup mode?");
        if (!ok) return;
        this._showToast("Wiping WiFi credentials…", "warning", TOAST_RESULT_MS);
        pressButton(this._config.wipe_wifi_entity);
      };
    }
    const otaBtn = sr.getElementById("btnOtaUpdate");
    if (otaBtn) {
      otaBtn.onclick = (e) => {
        e.preventDefault();
        e.stopPropagation();
        if (otaBtn.disabled) return;
        const entityId = otaBtn.dataset.entity;
        if (!entityId) return;
        this._startOtaUiSession(Date.now());
        this._showToast("OTA started…", "info", 0, { sticky: true });
        pressButton(entityId);
      };
    }

    const seedDraft = (key, entityId, force = false) => {
      if (!entityId) return;
      if (force || this._draft[key] === null || this._draft[key] === undefined || this._draft[key] === "") {
        this._draft[key] = this._safeText(this._state(entityId), "");
      }
    };

    const updateDraft = (key, inputId) => {
      const el = sr.getElementById(inputId);
      if (!el) return null;
      el.addEventListener("input", (e) => {
        e.stopPropagation();
        this._draft[key] = e.target.value;
      });
      el.addEventListener("focus", (e) => {
        e.stopPropagation();
        this._editing[key] = true;
        if (key === "dryValue" || key === "wetValue") {
          this._activeEditKey = key;
        }
      });
      return el;
    };

    const startCalEdit = (key, draftKey, displayEntityId) => {
      const setEntityId = this._getCalSetEntityId(draftKey);
      const setDomain = this._domain(setEntityId);
      const editable = !!setEntityId && (setDomain === "number" || setDomain === "input_number");
      if (!editable) return;
      const seedEntityId = displayEntityId || setEntityId;

      this._editing[draftKey] = true;
      this._activeEditKey = draftKey;
      seedDraft(draftKey, seedEntityId, true);
      this._focusAfterRender = `wt-${key}-input`;
      this._render();
    };

    const dryEdit = sr.getElementById("wt-dry-edit");
    if (dryEdit) {
      dryEdit.onclick = (e) => {
        e.preventDefault();
        e.stopPropagation();
        startCalEdit("dry", "dryValue", this._config.cal_dry_value_entity);
      };
    }
    const wetEdit = sr.getElementById("wt-wet-edit");
    if (wetEdit) {
      wetEdit.onclick = (e) => {
        e.preventDefault();
        e.stopPropagation();
        startCalEdit("wet", "wetValue", this._config.cal_wet_value_entity);
      };
    }

    updateDraft("dryValue", "wt-dry-input");
    updateDraft("wetValue", "wt-wet-input");
    const dryInput = sr.getElementById("wt-dry-input");
    if (dryInput) {
      dryInput.addEventListener("keydown", (e) => {
        if (e.key === "Enter") {
          e.preventDefault();
          e.stopPropagation();
          this._commitCalEdit("dryValue");
        } else if (e.key === "Escape") {
          e.preventDefault();
          e.stopPropagation();
          this._cancelCalEdit("dryValue");
        }
      });
    }
    const wetInput = sr.getElementById("wt-wet-input");
    if (wetInput) {
      wetInput.addEventListener("keydown", (e) => {
        if (e.key === "Enter") {
          e.preventDefault();
          e.stopPropagation();
          this._commitCalEdit("wetValue");
        } else if (e.key === "Escape") {
          e.preventDefault();
          e.stopPropagation();
          this._cancelCalEdit("wetValue");
        }
      });
    }
    const drySet = sr.getElementById("wt-dry-set");
    if (drySet) {
      drySet.onclick = (e) => {
        e.preventDefault();
        e.stopPropagation();
        this._commitCalEdit("dryValue");
      };
    }
    const wetSet = sr.getElementById("wt-wet-set");
    if (wetSet) {
      wetSet.onclick = (e) => {
        e.preventDefault();
        e.stopPropagation();
        this._commitCalEdit("wetValue");
      };
    }

    // Setup number fields with validation
    const attachNumberField = (inputId, saveId, errorId, draftKey, entityId) => {
      if (!entityId) return;
      const input = sr.getElementById(inputId);
      const save = sr.getElementById(saveId);
      const err = sr.getElementById(errorId);
      if (!input || !save) return;

      const setError = (msg) => {
        if (err) err.textContent = msg || "";
      };

      const getCurrent = () => this._num(entityId);

      const validate = () => {
        const raw = input.value?.trim() ?? "";
        const valid = raw.length > 0 && this._isNumberLike(raw) && Number(raw) > 0;
        const numVal = valid ? Number(raw) : NaN;
        const current = getCurrent();
        const unchanged = valid && current !== null && Math.abs(current - numVal) < 0.001;
        setError(valid ? "" : "Enter a number greater than 0");
        save.disabled = !valid || unchanged || !!this._pendingSet?.[draftKey];
        return { valid, numVal, unchanged };
      };

      const seed = () => {
        const v = this._state(entityId);
        if (!this._isUnknownState(v)) {
          this._draft[draftKey] = String(v);
          input.value = String(v);
        }
      };

      const cancel = () => {
        const v = this._state(entityId);
        if (!this._isUnknownState(v)) {
          this._draft[draftKey] = String(v);
          input.value = String(v);
        }
        this._editing[draftKey] = false;
        this._showToast(`Canceled ${this._labelForKey(draftKey)} edit`, "info", TOAST_RESULT_MS);
        validate();
      };

      const commit = () => {
        const { valid, unchanged } = validate();
        if (!valid || unchanged) return;
        this._editing[draftKey] = true;
        this._activeEditKey = draftKey;
        this._commitNumberEdit(draftKey, entityId, input.value, { label: this._labelForKey(draftKey) });
        validate();
      };

      input.addEventListener("input", (e) => {
        e.stopPropagation();
        this._draft[draftKey] = input.value;
        this._editing[draftKey] = true;
        validate();
      });

      input.addEventListener("focus", (e) => {
        e.stopPropagation();
        this._editing[draftKey] = true;
        if ((input.value ?? "").trim() === "") seed();
        validate();
      });

      input.addEventListener("keydown", (e) => {
        if (e.key === "Enter") {
          e.preventDefault();
          e.stopPropagation();
          commit();
        } else if (e.key === "Escape") {
          e.preventDefault();
          e.stopPropagation();
          cancel();
        }
      });

      input.addEventListener("blur", () => {
        if (this._pendingSet?.[draftKey]) return;
        commit();
        this._editing[draftKey] = false;
        if (this._activeEditKey === draftKey) this._activeEditKey = null;
      });

      save.addEventListener("click", (e) => {
        e.preventDefault();
        e.stopPropagation();
        commit();
      });

      validate();
    };

    attachNumberField("tankVolumeInput", "tankVolumeSave", "tankVolumeError", "tankVolume", this._config.tank_volume_entity);
    attachNumberField("rodLengthInput", "rodLengthSave", "rodLengthError", "rodLength", this._config.rod_length_entity);

    // Simulation controls
    const simToggle = sr.getElementById("simToggleBtn");
    if (simToggle) {
      simToggle.onclick = (e) => {
        e.preventDefault();
        e.stopPropagation();
        // Always only call select.select_option for sense_mode_entity.
        const entityId = this._config.sense_mode_entity;
        if (!entityId) return;
        if (this._pendingSet?.senseMode) return;
        const currentMode = this._senseModeUiValue();
        const nextMode = String(currentMode).toLowerCase() === "sim" ? "touch" : "sim";
        this._setPendingSenseMode(nextMode);
        this._callService("select", "select_option", { entity_id: entityId, option: nextMode });
      };
    }

    const simMode = sr.getElementById("simModeSelect");
    if (simMode) {
      simMode.onfocus = () => {
        this._suspendRender = true;
      };
      simMode.onblur = () => {
        this._suspendRender = false;
        if (this._deferredRender) {
          this._deferredRender = false;
          this._render();
        }
      };
      simMode.onchange = (e) => {
        e.preventDefault();
        e.stopPropagation();

        const entityId = simMode.dataset.entity;
        if (!entityId) return;

        const selected = simMode.value;

        const startedAt = Date.now();
        this._pendingSet.simMode = {
          entityId,
          verifyEntityId: entityId,
          targetValue: selected,
          startedAt,
          label: this._labelForKey("simMode"),
        };

        this._draft.simMode = selected;
        this._editing.simMode = true;
        this._showToast(`Setting simulation mode to ${this._safeText(selected, "")}…`, "info", 0, { sticky: true });

        const domain = this._domain(entityId);
        if (domain === "select") {
          this._callService("select", "select_option", { entity_id: entityId, option: selected });
        } else if (domain === "input_select") {
          this._callService("input_select", "select_option", { entity_id: entityId, option: selected });
        }

        setTimeout(() => {
          if (this._pendingSet?.simMode?.startedAt === startedAt) {
            this._checkPendingSets();
            if (this._modalOpen) this._render();
          }
        }, 5200);
      };
    }
  }

  // ---------- tank svg ----------
  _renderTankSvg(percent, ui) {
    const p = this._clamp(percent ?? 0, 0, 100);

    // If nullified, show an empty tank (outline only)
    const showFill = ui?.display !== "nullify";

    // The exported SVG uses a 200x200 viewBox.
    // The inner cavity clip path spans roughly from y=46.9 (top) to y=195.75 (bottom).
    const fillTop = 46.9;
    const fillBottom = 195.75;
    const fillRange = fillBottom - fillTop;

    const waterH = showFill ? (fillRange * p) / 100 : 0;
    const waterY = fillBottom - waterH;

    // A subtle gradient based on theme color (keeps the “modern app” look without hardcoded blues)
    const waterGradId = "wtWaterGrad";

    return `
    <svg class="tankSvg" viewBox="0 0 200 200" aria-label="Water tank level">
      <defs>
        <linearGradient id="linear-gradient" x1="90.76" y1="32.84" x2="90.76" y2="195.75" gradientUnits="userSpaceOnUse">
          <stop offset="0" stop-color="#b3b3b3"/>
          <stop offset=".17" stop-color="#aaa"/>
          <stop offset=".43" stop-color="#939393"/>
          <stop offset=".76" stop-color="#6d6d6d"/>
          <stop offset="1" stop-color="#4d4d4d"/>
        </linearGradient>
        <linearGradient id="linear-gradient-2" x1="4.92" y1="114.29" x2="176.6" y2="114.29" gradientUnits="userSpaceOnUse">
          <stop offset="0" stop-color="#000"/>
          <stop offset=".12" stop-color="#575757" stop-opacity=".75"/>
          <stop offset=".26" stop-color="#acacac" stop-opacity=".5"/>
          <stop offset=".39" stop-color="#e0e0e0" stop-opacity=".35"/>
          <stop offset=".49" stop-color="#f2f2f2" stop-opacity=".3"/>
          <stop offset=".6" stop-color="#ddd" stop-opacity=".36"/>
          <stop offset=".74" stop-color="#a2a2a2" stop-opacity=".53"/>
          <stop offset=".9" stop-color="#424242" stop-opacity=".81"/>
          <stop offset="1" stop-color="#000"/>
        </linearGradient>

        <!-- theme-based water gradient -->
        <linearGradient id="${waterGradId}" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stop-color="var(--primary-color)" stop-opacity="0.75" />
          <stop offset="100%" stop-color="var(--primary-color)" stop-opacity="0.25" />
        </linearGradient>

        <clipPath id="clippath">
          <path id="tank-water-clip" d="M4.96,46.9v138.94c0,5.61,4.53,10.16,10.11,10.16h151.38c5.58,0,10.11-4.55,10.11-10.16V46.47c0-3.46,2.3-4.96-5.93-6.88-30.19-7.02-66.32-7.02-79.87-7.01s-49.67-.01-79.78,6.88c-8.34,1.91-6.02,4.04-6.02,7.44Z" fill="none"/>
        </clipPath>
      </defs>

      <g id="_2D">
        <g id="tank-main">
          <path d="M15.07,195.75c-5.44,0-9.86-4.45-9.86-9.91V46.9c0-.66-.08-1.27-.17-1.85-.33-2.32-.54-3.85,5.99-5.35,29.86-6.83,65.87-6.86,79.42-6.87h2.01c11.87,0,47.99,0,78.11,7,6.39,1.49,6.22,2.65,5.91,4.77-.08.55-.17,1.17-.17,1.86v139.37c0,5.46-4.42,9.91-9.86,9.91H15.07Z" fill="url(#linear-gradient)" opacity=".31"/>
          <path d="M15.07,195.75c-5.44,0-9.86-4.45-9.86-9.91V46.9c0-.66-.08-1.27-.17-1.85-.33-2.32-.54-3.85,5.99-5.35,29.86-6.83,65.87-6.86,79.42-6.87h2.01c11.87,0,47.99,0,78.11,7,6.39,1.49,6.22,2.65,5.91,4.77-.08.55-.17,1.17-.17,1.86v139.37c0,5.46-4.42,9.91-9.86,9.91H15.07Z" fill="url(#linear-gradient-2)" opacity=".3"/>
          <path d="M92.45,33.09c11.87,0,47.98,0,78.07,6.99,6.17,1.43,6.02,2.46,5.72,4.49-.08.56-.17,1.18-.17,1.9v139.37c0,5.33-4.31,9.66-9.61,9.66H15.07c-5.3,0-9.61-4.33-9.61-9.66V46.9c0-.68-.09-1.32-.17-1.88-.32-2.26-.51-3.63,5.8-5.07,29.83-6.82,65.81-6.85,79.36-6.86h.31s1.68,0,1.68,0M92.45,32.59c-.59,0-1.15,0-1.68,0-13.55.01-49.67-.01-79.78,6.88-8.34,1.91-6.02,4.04-6.02,7.44v138.94c0,5.61,4.53,10.16,10.11,10.16h151.38c5.58,0,10.11-4.55,10.11-10.16V46.47c0-3.46,2.3-4.96-5.93-6.88-29.02-6.74-63.52-7.01-78.18-7.01h0Z"/>
          <path d="M92.45,32.59c14.67,0,49.17.26,78.18,7.01,8.23,1.91,5.93,3.42,5.93,6.88v139.37c0,5.61-4.53,10.16-10.11,10.16H15.07c-5.58,0-10.11-4.55-10.11-10.16V46.9c0-3.4-2.33-5.53,6.02-7.44,30.11-6.89,66.24-6.87,79.78-6.88.53,0,1.09,0,1.68,0M92.45,30.09h-1.68s-.31,0-.31,0c-13.64.01-49.87.04-80.03,6.94-7.56,1.73-8.68,4.29-8.1,8.41.07.51.14.99.14,1.46v138.94c0,6.98,5.66,12.66,12.61,12.66h151.38c6.95,0,12.61-5.68,12.61-12.66V46.47c0-.5.07-.97.14-1.46.65-4.44-1.22-6.27-8.01-7.85-30.43-7.07-66.8-7.07-78.75-7.07h0Z" fill="#e6e6e6"/>
          <path d="M92.45,32.59c14.67,0,49.17.26,78.18,7.01,8.23,1.91,5.93,3.42,5.93,6.88v139.37c0,5.61-4.53,10.16-10.11,10.16H15.07c-5.58,0-10.11-4.55-10.11-10.16V46.9c0-3.4-2.33-5.53,6.02-7.44,30.11-6.89,66.24-6.87,79.78-6.88.53,0,1.09,0,1.68,0M92.45,30.59h-1.68s-.31,0-.31,0c-13.62.01-49.81.04-79.92,6.93-7.22,1.65-8.26,3.94-7.71,7.85.07.53.14,1.03.14,1.53v138.94c0,6.71,5.43,12.16,12.11,12.16h151.38c6.68,0,12.11-5.46,12.11-12.16V46.47c0-.53.07-.97.15-1.54.61-4.14-1.1-5.77-7.63-7.29-30.37-7.06-66.7-7.06-78.64-7.06h0Z" fill="#999"/>
        </g>

        <g id="tank-ribs" opacity=".3">
          <line x1="13.33" y1="40.33" x2="13.33" y2="195.33" fill="none" stroke="#4d4d4d" stroke-linejoin="round" stroke-width="3"/>
          <line x1="13.33" y1="40.33" x2="13.33" y2="195.33" fill="none" stroke="#999" stroke-linejoin="round" stroke-width="2"/>
          <line x1="13.33" y1="40.33" x2="13.33" y2="195.33" fill="none" stroke="#e6e6e6" stroke-linejoin="round" stroke-width=".5"/>
          <line x1="33.33" y1="36.33" x2="33.33" y2="194.83" fill="none" stroke="#4d4d4d" stroke-linejoin="round" stroke-width="3"/>
          <line x1="33.33" y1="36.33" x2="33.33" y2="194.83" fill="none" stroke="#999" stroke-linejoin="round" stroke-width="2"/>
          <line x1="33.33" y1="36.33" x2="33.33" y2="194.83" fill="none" stroke="#e6e6e6" stroke-linejoin="round" stroke-width=".5"/>
          <line x1="145.33" y1="36.33" x2="145.33" y2="195.33" fill="none" stroke="#4d4d4d" stroke-linejoin="round" stroke-width="3"/>
          <line x1="145.33" y1="36.33" x2="145.33" y2="195.33" fill="none" stroke="#999" stroke-linejoin="round" stroke-width="2"/>
          <line x1="145.33" y1="36.33" x2="145.33" y2="195.33" fill="none" stroke="#e6e6e6" stroke-linejoin="round" stroke-width=".5"/>
          <line x1="114.33" y1="34.33" x2="114.33" y2="195.33" fill="none" stroke="#4d4d4d" stroke-linejoin="round" stroke-width="3"/>
          <line x1="114.33" y1="34.33" x2="114.33" y2="195.33" fill="none" stroke="#999" stroke-linejoin="round" stroke-width="2"/>
          <line x1="114.33" y1="34.33" x2="114.33" y2="195.33" fill="none" stroke="#e6e6e6" stroke-linejoin="round" stroke-width=".5"/>
          <line x1="64.33" y1="33.83" x2="64.33" y2="194.83" fill="none" stroke="#4d4d4d" stroke-linejoin="round" stroke-width="3"/>
          <line x1="64.33" y1="33.83" x2="64.33" y2="194.83" fill="none" stroke="#999" stroke-linejoin="round" stroke-width="2"/>
          <line x1="64.33" y1="33.83" x2="64.33" y2="194.83" fill="none" stroke="#e6e6e6" stroke-linejoin="round" stroke-width=".5"/>
          <line x1="165.33" y1="39.33" x2="165.33" y2="195.33" fill="none" stroke="#4d4d4d" stroke-linejoin="round" stroke-width="3"/>
          <line x1="165.33" y1="39.33" x2="165.33" y2="195.33" fill="none" stroke="#999" stroke-linejoin="round" stroke-width="2"/>
          <line x1="165.33" y1="39.33" x2="165.33" y2="195.33" fill="none" stroke="#e6e6e6" stroke-linejoin="round" stroke-width=".5"/>
        </g>

        <!-- Dynamic water: a simple rect clipped to the tank cavity -->
        <g clip-path="url(#clippath)">
          <rect id="tank-water-fill" x="0" y="${waterY.toFixed(2)}" width="200" height="${waterH.toFixed(2)}" fill="url(#${waterGradId})" opacity="0.9"></rect>
          ${showFill && waterH > 0.5
        ? `<line id="tank-water-surface" x1="10" y1="${waterY.toFixed(2)}" x2="170" y2="${waterY.toFixed(2)}"></line>`
        : ``
      }
        </g>

        <g id="tank-tap">
          <g id="Tap_outline">
            <path d="M192.5,181.16c0-1.85,0-3.59-1.22-4.8-1.05-1.05-2.84-1.54-5.63-1.54h-8.82v-5.99h8.82c11.57,0,12.79,5.77,12.84,12.33h-6Z" fill="#999"/>
            <path d="M185.66,169.33c10.84,0,12.22,5.05,12.34,11.34h-4.99c0-1.77-.13-3.41-1.36-4.65-1.15-1.15-3.05-1.69-5.98-1.69h-8.32v-4.99h8.32M185.66,168.33h-9.32v7h9.32c6.62,0,6.34,2.84,6.34,6.34h7c0-6.99-1.21-13.33-13.34-13.34h0Z" fill="gray"/>
            <path d="M199,181.66h-7c0-3.5.28-6.34-6.34-6.34h-9.32v-6.99h9.32c12.14,0,13.34,6.34,13.34,13.33Z" fill="none" stroke="#000" stroke-miterlimit="10" stroke-width=".5"/>
          </g>
          <path id="tap_handle" d="M184.83,165.99h-2.33c-1.17,1.17-1.17-.52-1.17-1.17h0c0-.64,0-2.33,1.17-1.17h6.99c1.17-1.17,1.17.52,1.17,1.17h0c0,.64,0,2.33-1.17,1.17h-2.33" fill="#e0b12b" stroke="#000" stroke-miterlimit="10" stroke-width=".5"/>
          <polygon id="tap_neck" points="184.82 165.98 184.84 168.31 187.16 168.33 187.16 166 184.82 165.98" fill="#87453d" stroke="#000" stroke-miterlimit="10" stroke-width=".5"/>
        </g>

        <path id="tank-man-hole" d="M104.6,31.13h0c0,.95.74,1.74,1.69,1.8l37.19,2.32c1.04.06,1.92-.76,1.92-1.8v-2.32c0-1-.81-1.8-1.8-1.8h-37.19c-1,0-1.8.81-1.8,1.8Z" fill="#999" stroke="#000" stroke-miterlimit="10" stroke-width=".5"/>
      </g>
    </svg>`;
  }

  // ---------- gauge ----------
  _renderGaugeArc(percent) {
    const size = 130;
    const stroke = 12;
    const r = (size - stroke) / 2;
    const cx = size / 2;
    const cy = size / 2;

    // 270° arc (from 135° to 405°)
    const startAngle = (135 * Math.PI) / 180;
    const endAngle = (405 * Math.PI) / 180;
    const arcLen = endAngle - startAngle;

    const p = this._clamp(percent ?? 0, 0, 100) / 100;
    const a = startAngle + arcLen * p;

    const polar = (ang) => ({
      x: cx + r * Math.cos(ang),
      y: cy + r * Math.sin(ang),
    });

    const s = polar(startAngle);
    const e = polar(endAngle);
    const v = polar(a);

    // Large arc flag for SVG
    const largeBg = 1; // always > 180° for the full background arc
    const largeVal = p > 0.5 ? 1 : 0;

    const bgPath = `M ${s.x} ${s.y} A ${r} ${r} 0 ${largeBg} 1 ${e.x} ${e.y}`;
    const valPath = `M ${s.x} ${s.y} A ${r} ${r} 0 ${largeVal} 1 ${v.x} ${v.y}`;

    return `
      <svg width="${size}" height="${size}" viewBox="0 0 ${size} ${size}" aria-hidden="true">
        <path d="${bgPath}" fill="none" stroke="rgba(0,0,0,0.10)" stroke-width="${stroke}" stroke-linecap="round"></path>
        <path d="${valPath}" fill="none" stroke="var(--primary-color)" stroke-width="${stroke}" stroke-linecap="round"></path>
      </svg>
    `;
  }

  _render() {
    if (!this._hass || !this._config) return;

    const resState = this._resolutionState?.status || "done";

    if (resState === "pending" || resState === "loading") {
      this._renderLoadingCard(this._config.title, "Resolving device entities…");
      return;
    }

    const missingKeys = REQUIRED_ENTITY_KEYS.filter((k) => !this._config[k]);
    if (missingKeys.length || resState === "error" || resState === "incomplete") {
      const msg = this._resolutionState?.error || `Missing entities: ${missingKeys.join(", ")}`;
      if (!this._warnedResolution) {
        // eslint-disable-next-line no-console
        console.warn(`${CARD_TAG}: configuration incomplete`, msg);
        this._warnedResolution = true;
      }
      this._renderErrorCard(this._config.title, msg);
      return;
    }

    let modalScrollTop = null;
    let modalFocusId = null;
    let modalFocusSelection = null;

    if (this._modalOpen && this.shadowRoot) {
      const modalCard = this.shadowRoot.querySelector(".wt-modal-card");
      if (modalCard) modalScrollTop = modalCard.scrollTop;
      const activeEl = this.shadowRoot.activeElement;
      if (activeEl && this.shadowRoot.contains(activeEl) && activeEl.id) {
        modalFocusId = activeEl.id;
        const tag = activeEl.tagName;
        const isTextInput = tag === "INPUT" || tag === "TEXTAREA";
        if (isTextInput && typeof activeEl.selectionStart === "number" && typeof activeEl.selectionEnd === "number") {
          modalFocusSelection = { start: activeEl.selectionStart, end: activeEl.selectionEnd };
        }
      }
    }

    const ui = this._computeUiState();


    const pct = this._num(this._config.percent_entity);
    const liters = this._num(this._config.liters_entity);
    const cm = this._num(this._config.cm_entity);
    const raw = this._state(this._config.raw_entity);

    const statusState = this._state(this._config.status_entity);
    const onlineText = this._isOnlineState(statusState) ? "Online" : "Offline";

    // Simulation mode enabled? Prefer device sense_mode when available so UI mirrors the device truth.
    const simEnabled = this._config.sense_mode_entity
      ? this._senseModeIsSimValue(this._senseModeUiValue())
      : false;
    const probeState = this._state(this._config.probe_entity);

    const now = Date.now();
    const updateAvailableState = this._config.update_available_entity ? this._state(this._config.update_available_entity) : null;
    const updateAvailable = this._isTruthyState(updateAvailableState);
    const otaState = this._config.ota_state_entity ? this._state(this._config.ota_state_entity) : null;
    const otaProgressVal = this._config.ota_progress_entity ? this._num(this._config.ota_progress_entity) : null;
    const otaLastMessage = this._config.ota_last_message_entity ? this._state(this._config.ota_last_message_entity) : null;
    this._updateOtaUiSession(otaState, otaProgressVal, otaLastMessage, now);
    const otaStateKnown = this._config.ota_state_entity && !this._isUnknownState(otaState);
    const otaStateLower = otaStateKnown ? String(otaState).trim().toLowerCase() : "";
    const otaSessionActive = !!this._otaUi?.active;
    const otaInProgress = otaStateLower === "downloading" || otaStateLower === "verifying" || otaStateLower === "applying" || otaSessionActive;
    const otaActive = otaStateLower === "downloading" || otaStateLower === "verifying" || otaStateLower === "applying" || otaStateLower === "rebooting" || otaSessionActive;
    const otaProgressEff = otaProgressVal === null ? this._otaUi?.lastProgress : otaProgressVal;
    const otaProgressPct = (otaProgressEff !== null && otaProgressEff !== undefined && otaProgressEff >= 1 && otaProgressEff <= 100)
      ? Math.round(otaProgressEff)
      : null;
    const otaProgressText = otaProgressPct !== null ? `${otaProgressPct}%` : "—";
    const otaStale = this._otaSessionIsStale(now);
    const otaStatusText = (() => {
      if (otaStale) return "Rebooting / reconnecting…";
      switch (otaStateLower) {
        case "downloading": return "Downloading";
        case "verifying": return "Verifying";
        case "applying": return "Applying";
        case "rebooting": return "Rebooting";
        case "queued": return "Queued";
        default:
          if (otaSessionActive && this._otaUi?.lastState === "queued") return "Queued";
          return this._safeText(otaState);
      }
    })();
    const otaInline = otaActive ? `
      <div class="otaRow">
        <ha-icon icon="mdi:update"></ha-icon>
        <div class="otaText">Updating… ${this._safeText(otaStatusText)}</div>
        <div class="otaPct">${otaProgressText}</div>
      </div>
      <div class="otaBar">
        <div class="otaBarFill ${otaProgressPct !== null ? "" : "otaBarFill--ind"}" style="${otaProgressPct !== null ? `width:${otaProgressPct}%;` : ""}"></div>
      </div>
    ` : "";
    const currentFw = this._config.fw_version_entity ? this._state(this._config.fw_version_entity) : null;
    const targetFw = this._config.ota_target_version_entity ? this._state(this._config.ota_target_version_entity) : null;
    const showUpdateBanner = updateAvailable && !otaInProgress;
    const showVersionLine = !this._isUnknownState(currentFw) && !this._isUnknownState(targetFw);
    const versionLine = showVersionLine ? `${this._safeText(currentFw, "")} → ${this._safeText(targetFw, "")}` : "";
    const updateBanner = showUpdateBanner ? `
      <div class="notice" id="otaBanner" role="button" tabindex="0" style="cursor:pointer;">
        <div>
          <div class="msg">Update available</div>
          ${versionLine ? `<div style="font-size:12px;opacity:0.7;">${versionLine}</div>` : ``}
        </div>
        <ha-icon icon="mdi:chevron-right" style="margin-left:auto;"></ha-icon>
      </div>
    ` : "";

    const css = `
      :host { display:block; }
      ha-card {
        border-radius: 20px;
        padding: 16px;
        overflow: hidden;
      }

      ha-card.simulation {
        box-shadow: inset 0 0 0 2px rgba(255, 200, 0, 0.45);
      }

      .header {
        display:flex;
        align-items:flex-start;
        justify-content:space-between;
        gap:12px;
        margin-bottom: 10px;
      }

      .title {
        font-size: 18px;
        font-weight: 800;
        line-height: 1.2;
      }

      .titleWrap {
        display:flex;
        align-items:center;
        gap:8px;
      }

      .badge {
        font-size: 11px;
        font-weight: 900;
        letter-spacing: 0.6px;
        padding: 2px 8px;
        border-radius: 999px;
        background: rgba(255, 165, 0, 0.18);
        border: 1px solid rgba(255, 165, 0, 0.35);
      }
        
      .badge ha-icon {
        --mdc-icon-size: 14px;
      }

      .gaugeWrap {
        position: relative;
      }

      .gaugeOverlay {
        position: absolute;
        inset: 0;
        display: flex;
        align-items: center;
        justify-content: center;
        pointer-events: none;
      }

      .gaugeOverlay .pill {
        display: inline-flex;
        align-items: center;
        gap: 6px;
        padding: 6px 10px;
        border-radius: 999px;
        font-weight: 800;
        font-size: 13px;
      }

      .gaugeOverlay.warn .pill {
        background: rgba(255, 170, 0, 0.85);
        color: #000;
      }

      .gaugeOverlay.error .pill {
        background: rgba(255, 64, 64, 0.9);
        color: #fff;
      }

      .tankSvg {
        display: block;
        width: 140px;
        height: 140px;
      }

      /* Make the exported tank feel modern + theme-aware */
      #tank-water-fill {
        filter: saturate(1.05);
      }

      #tank-water-surface {
        stroke: rgba(255,255,255,0.45);
        stroke-width: 2;
        stroke-linecap: round;
        opacity: 0.6;
        vector-effect: non-scaling-stroke;
      }

      /* Ribs were exported with multiple strokes; keep them subtle */
      #tank-ribs {
        opacity: 0.12;
      }

      .dim {
        opacity: 0.55;
      }

      .simOffBtn {
        cursor: pointer;
        border: none;
        border-radius: 999px;
        padding: 2px 10px;
        font-size: 11px;
        font-weight: 800;
        letter-spacing: 0.3px;
        background: rgba(0,0,0,0.06);
        opacity: 0.85;
      }
      .simOffBtn:hover { opacity: 1; }

      .corner {
        font-size: 12px;
        opacity: 0.85;
      }

      .layout {
        display:grid;
        grid-template-columns: 140px 1fr;
        gap: 14px;
        align-items:center;
      }

      .pctText {
        font-size: 34px;
        font-weight: 900;
        line-height: 1;
        margin-top: -4px;
      }

      .metrics {
        display:flex;
        flex-direction:column;
        gap: 10px;
      }

      .metricRow {
        display:flex;
        gap: 10px;
        align-items:center;
        font-size: 14px;
      }

      .metricRow b {
        font-size: 18px;
      }

      .footer {
        display:flex;
        justify-content:space-between;
        align-items:center;
        gap:12px;
        margin-top: 12px;
        font-size: 12px;
        opacity: 0.92;
      }

      .simNote {
        font-size: 12px;
        opacity: 0.78;
        margin-top: 10px;
      }

      .warn {
        display:flex;
        gap: 8px;
        align-items:center;
      }

      .btn {
        cursor:pointer;
        border:none;
        border-radius: 999px;
        padding: 10px 12px;
        background: rgba(0,0,0,0.06);
      }

      .notice {
        display:flex;
        gap: 10px;
        align-items:center;
        padding: 14px;
        border-radius: 16px;
        background: rgba(255, 170, 0, 0.12);
      }
      .notice.error {
        background: rgba(255, 0, 0, 0.10);
      }
      .notice .msg {
        font-weight: 800;
        font-size: 16px;
      }
      .otaRow {
        display: flex;
        align-items: center;
        gap: 8px;
        padding: 10px 12px;
        border-radius: 12px;
        background: rgba(0,0,0,0.06);
        margin-top: 10px;
      }
      .otaRow ha-icon {
        --mdc-icon-size: 18px;
      }
      .otaText {
        flex: 1;
        font-size: 13px;
        font-weight: 700;
      }
      .otaPct {
        font-size: 12px;
        font-weight: 800;
        opacity: 0.8;
      }
      .otaBar {
        height: 4px;
        border-radius: 999px;
        background: rgba(0,0,0,0.08);
        overflow: hidden;
        margin-top: 6px;
      }
      .otaBarFill {
        height: 100%;
        background: var(--primary-color);
        width: 0;
        transition: width 0.2s ease;
      }
      .otaBarFill--ind {
        width: 40%;
        animation: otaIndeterminate 1.2s infinite;
      }
      @keyframes otaIndeterminate {
        0% { transform: translateX(-60%); }
        100% { transform: translateX(160%); }
      }
      .rawLine {
        font-size: 13px;
        opacity: 0.85;
        margin-top: 10px;
      }

      /* Modal */
      .wt-modal-overlay {
        position: fixed;
        inset: 0;
        z-index: 9999;
        background: rgba(0, 0, 0, 0.45);
        display: flex;
        align-items: center;
        justify-content: center;
        padding: 16px;
      }
      .wt-modal-card {
        background: var(--card-background-color, #fff);
        color: var(--primary-text-color, #111);
        width: min(92vw, 420px);
        max-height: 80vh;
        overflow: auto;
        border-radius: 18px;
        box-shadow: 0 10px 40px rgba(0, 0, 0, 0.22);
        padding: 18px;
        box-sizing: border-box;
      }
      .wt-modal-header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 10px;
        margin-bottom: 10px;
      }
      .wt-modal-title {
        font-size: 18px;
        font-weight: 900;
      }
      .wt-modal-actions {
        display: flex;
        align-items: center;
        gap: 8px;
      }
      .wt-modal-close {
        cursor: pointer;
        border: none;
        background: transparent;
        font-size: 18px;
        line-height: 1;
        padding: 6px;
      }
      .wt-badge {
        padding: 3px 9px;
        border-radius: 999px;
        font-weight: 800;
        font-size: 11px;
        border: 1px solid rgba(0,0,0,0.12);
      }
      .wt-toast {
        display: inline-flex;
        align-items: center;
        gap: 6px;
        padding: 6px 10px;
        border-radius: 999px;
        font-size: 12px;
        font-weight: 700;
        background: var(--secondary-background-color, rgba(0,0,0,0.06));
        border: 1px solid var(--divider-color, rgba(127,127,127,0.35));
      }
      .wt-toast-float {
        position: fixed;
        left: 50%;
        bottom: 24px;
        transform: translateX(-50%);
        z-index: 10000;
        pointer-events: none;
      }
      .wt-toast--info {
        color: var(--primary-text-color);
      }
      .wt-toast--success {
        color: var(--primary-text-color);
        background: rgba(0,150,0,0.16);
        border-color: rgba(0,150,0,0.35);
      }
      .wt-toast--warn {
        color: var(--primary-text-color);
        background: rgba(255,170,0,0.18);
        border-color: rgba(255,170,0,0.4);
      }
      .wt-toast--error {
        color: var(--primary-text-color);
        background: rgba(255,64,64,0.18);
        border-color: rgba(255,64,64,0.4);
      }
      .wt-warnings {
        margin-bottom: 12px;
        font-size: 14px;
      }
      .wt-warning-list {
        list-style: none;
        padding: 0;
        margin: 0;
        display: flex;
        flex-direction: column;
        gap: 8px;
      }
      .wt-warning-list li {
        display: flex;
        align-items: center;
        gap: 8px;
      }
      .wt-all-good {
        display: flex;
        align-items: center;
        gap: 8px;
        font-weight: 700;
      }
      .wt-section {
        margin-top: 12px;
        padding-top: 8px;
        border-top: 1px solid rgba(0,0,0,0.08);
      }
      .wt-section:first-of-type {
        border-top: none;
        padding-top: 0;
      }
      .wt-section-head {
        display: flex;
        justify-content: space-between;
        gap: 10px;
        align-items: flex-start;
      }
      .wt-section-title {
        font-size: 16px;
        font-weight: 800;
      }
      .wt-section-sub {
        font-size: 13px;
        opacity: 0.8;
      }
      .wt-icon-btn {
        border: none;
        background: transparent;
        cursor: pointer;
        padding: 4px;
      }
      .wt-cal-row {
        display: grid;
        grid-template-columns: 1fr 1fr;
        gap: 10px;
        margin-top: 12px;
      }
      .wt-tile {
        border: 1px solid rgba(0,0,0,0.1);
        border-radius: 12px;
        padding: 12px;
        display: flex;
        gap: 8px;
        align-items: center;
        justify-content: center;
        cursor: pointer;
        background: rgba(0,0,0,0.02);
      }
      .wt-tile.disabled {
        opacity: 0.5;
        cursor: not-allowed;
      }
      .wt-note {
        margin-top: 8px;
        font-size: 12px;
        opacity: 0.75;
      }
      .wt-cal-values {
        display: grid;
        grid-template-columns: 1fr 1fr;
        gap: 10px;
        margin-top: 12px;
      }
      .wt-cal-value {
        font-size: 13px;
        display: flex;
        flex-direction: column;
        gap: 6px;
      }
      .wt-cal-label {
        opacity: 0.8;
      }
      .wt-cal-input-row {
        display: flex;
        gap: 6px;
        align-items: center;
      }
      .wt-cal-secondary {
        font-size: 12px;
        opacity: 0.7;
        margin-top: -2px;
      }
      .wt-cal-edit {
        flex: 1;
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 8px;
        border-radius: 8px;
        padding: 8px;
        background: var(--input-fill-color, var(--secondary-background-color, rgba(0,0,0,0.06)));
        border: 1px solid var(--divider-color, rgba(127,127,127,0.35));
        color: var(--primary-text-color);
        cursor: pointer;
        font-size: 14px;
        text-align: left;
      }
      .wt-cal-edit-hint {
        font-size: 12px;
        font-weight: 700;
        opacity: 0.6;
      }
      .wt-cal-input-row,
      .wt-cal-input-row input,
      .wt-setup-input,
      .wt-setup-input input {
        min-width: 0;
      }
      .wt-cal-input-row input,
      .wt-setup-input input,
      .wt-setup-input select {
        flex: 1;
        border-radius: 8px;
        border: 1px solid var(--divider-color, rgba(127,127,127,0.35));
        padding: 8px;
        font-size: 14px;
        background: var(--input-fill-color, var(--secondary-background-color, rgba(0,0,0,0.06)));
        color: var(--primary-text-color);
      }
      .wt-modal-card input,
      .wt-modal-card select {
        box-sizing: border-box;
        width: 100%;
      }
      .wt-modal-card input::placeholder {
        color: var(--secondary-text-color);
      }
      .wt-modal-card input:focus,
      .wt-modal-card select:focus,
      .wt-cal-edit:focus {
        outline: none;
        border-color: var(--primary-color);
        box-shadow: 0 0 0 2px color-mix(in srgb, var(--primary-color) 30%, transparent);
      }
      .wt-mini-btn,
      .wt-btn {
        border: none;
        border-radius: 10px;
        padding: 8px 12px;
        cursor: pointer;
        background: rgba(0,0,0,0.08);
        font-weight: 700;
      }
      .wt-btn.on {
        background: rgba(0,150,0,0.15);
      }
      .wt-btn-danger {
        background: rgba(200,0,0,0.16);
        color: var(--primary-text-color);
      }
      .wt-toggle {
        position: relative;
        display: inline-flex;
        align-items: center;
        gap: 8px;
        padding: 6px 10px 6px 6px;
        border-radius: 16px;
        min-width: 70px;
        background: rgba(0,0,0,0.08);
        transition: background 0.2s ease;
        cursor: pointer;
        border: none;
        font-weight: 700;
      }
      .wt-toggle.on {
        background: rgba(0,150,0,0.18);
      }
      .wt-toggle-thumb {
        width: 26px;
        height: 26px;
        border-radius: 50%;
        background: white;
        box-shadow: 0 1px 4px rgba(0,0,0,0.25);
        transform: translateX(0);
        transition: transform 0.2s ease;
      }
      .wt-toggle.on .wt-toggle-thumb {
        transform: translateX(28px);
      }
      .wt-toggle-label {
        font-size: 13px;
        text-transform: capitalize;
      }
      .wt-setup {
        display: flex;
        flex-direction: column;
        gap: 10px;
        margin-top: 8px;
      }
      .wt-setup-row {
        display: grid;
        grid-template-columns: auto 1fr;
        gap: 10px;
        align-items: center;
      }
      .wt-setup-icon ha-icon {
        --mdc-icon-size: 22px;
        opacity: 0.8;
      }
      .wt-setup-label {
        font-weight: 700;
      }
      .wt-setup-input {
        display: flex;
        gap: 8px;
        align-items: center;
        margin-top: 6px;
      }
      .wt-unit {
        font-size: 12px;
        font-weight: 700;
        opacity: 0.7;
        padding: 0 2px;
        white-space: nowrap;
      }
      .wt-setup-help {
        font-size: 12px;
        opacity: 0.75;
        margin-top: 4px;
      }
      .wt-error {
        min-height: 16px;
        color: var(--error-color, #c00);
        font-size: 12px;
        margin-top: 2px;
      }
      input:disabled,
      button:disabled,
      select:disabled {
        opacity: 0.55;
        cursor: not-allowed;
      }
      .wt-modal-footer {
        margin-top: 14px;
        display: flex;
        justify-content: flex-end;
      }
      .wt-link {
        border: none;
        background: none;
        color: var(--primary-color);
        cursor: pointer;
        display: inline-flex;
        align-items: center;
        gap: 6px;
        font-weight: 700;
        padding: 4px;
      }
      .wt-diag-list {
        display: flex;
        flex-direction: column;
        gap: 6px;
        font-size: 13px;
      }
      .wt-diag-row {
        display: flex;
        justify-content: space-between;
        gap: 10px;
      }
      @media (max-width: 379px) {
        .wt-cal-values {
          grid-template-columns: 1fr;
        }
        .wt-cal-row {
          grid-template-columns: 1fr;
        }
      }
    `;

    const nullify = ui.display === "nullify";
    const dim = ui.display === "dim";

    const pctText = nullify
      ? "—%"
      : (pct !== null ? `${this._formatNumber(pct, CARD_PERCENT_DECIMALS)}%` : "—%");
    const volumeDisplay = nullify ? { text: "—", unit: "L" } : this._formatVolume(liters);
    const heightDisplay = nullify ? { text: "—", unit: "cm" } : this._formatHeight(cm);

    const view = (this._config.view || "tank").toLowerCase();
    const visualPct = nullify ? 0 : (pct ?? 0);
    const visual =
      view === "gauge"
        ? this._renderGaugeArc(visualPct)
        : this._renderTankSvg(visualPct, ui);

    const overlay =
      ui.severity === "ok"
        ? ""
        : `<div class="gaugeOverlay ${ui.severity}">
                    <div class="pill"><ha-icon icon="${ui.icon}"></ha-icon>${this._safeText(ui.msg)}</div>
                   </div>`;

    const footerIcon =
      ui.severity === "ok"
        ? "mdi:check-circle-outline"
        : ui.severity === "warn"
          ? "mdi:alert-outline"
          : "mdi:alert-circle-outline";
    const footerMsg = ui.msg || (ui.severity === "ok" ? "All readings valid" : "");

    const bodyHtml = `
        <div class="layout ${dim ? "dim" : ""}">
          <div class="gaugeWrap">
            ${visual}
            ${overlay}
          </div>

          <div class="metrics">
            <div class="pctText">${pctText}</div>

            <div class="metricRow">
              <ha-icon icon="mdi:water-outline"></ha-icon>
              <div><b>${volumeDisplay.text}</b> ${volumeDisplay.unit}</div>
            </div>

            <div class="metricRow">
              <ha-icon icon="mdi:ruler"></ha-icon>
              <div><b>${heightDisplay.text}</b> ${heightDisplay.unit}</div>
            </div>
          </div>
        </div>

        ${updateBanner}

        <div class="rawLine">Raw: ${this._safeText(raw)}</div>

        ${otaInline}

        <div class="footer">
          <div class="warn">
            <ha-icon icon="${footerIcon}"></ha-icon>
            <div>${footerMsg}</div>
          </div>

          <button class="btn" id="settingsBtn" title="Settings">
            <ha-icon icon="mdi:cog"></ha-icon>
          </button>
        </div>
        ${simEnabled ? `<div class="simNote">Values are simulated</div>` : ``}
      `;

    const html = `
      <style>${css}</style>
      <ha-card class="${simEnabled ? "simulation" : ""}">
        <div class="header">
          <div class="titleWrap">
            <div class="title">${this._config.title}</div>
            ${simEnabled
        ? `
                  <span class="badge"><ha-icon icon="mdi:alert-outline"></ha-icon> SIMULATION</span>
                  <button class="simOffBtn" id="simOffBtn" title="Turn simulation off">Sim off</button>
                `
        : ``
      }
          </div>
          <div class="corner">${onlineText}</div>
        </div>
        ${bodyHtml}
        ${this._renderModalHtml()}
      </ha-card>
    `;

    this.shadowRoot.innerHTML = html;
    this._bindModalHandlers();

    const settingButtons = this.shadowRoot.querySelectorAll("#settingsBtn");
    settingButtons.forEach((btn) => {
      btn.onclick = () => this._openModal("main");
    });

    const simOff = this.shadowRoot.getElementById("simOffBtn");
    if (simOff && this._config.sense_mode_entity) {
      simOff.onclick = (e) => {
        e.stopPropagation();
        if (this._pendingSet?.senseMode) return;
        this._setPendingSenseMode("touch");
        this._hass.callService("select", "select_option", {
          entity_id: this._config.sense_mode_entity,
          option: "touch",
        });
      };
    }

    const otaBanner = this.shadowRoot.getElementById("otaBanner");
    if (otaBanner) {
      otaBanner.onclick = (e) => {
        e.preventDefault();
        e.stopPropagation();
        this._scrollToOta = true;
        this._openModal("advanced");
      };
      otaBanner.onkeydown = (e) => {
        if (e.key === "Enter" || e.key === " ") {
          e.preventDefault();
          this._scrollToOta = true;
          this._openModal("advanced");
        }
      };
    }

    this._bindModalHandlers();

    if (this._modalOpen && this.shadowRoot) {
      requestAnimationFrame(() => {
        if (!this._modalOpen || !this.shadowRoot) return;
        const modalCard = this.shadowRoot.querySelector(".wt-modal-card");
        if (modalCard && modalScrollTop !== null) {
          modalCard.scrollTop = modalScrollTop;
          setTimeout(() => {
            if (modalCard.isConnected) modalCard.scrollTop = modalScrollTop;
          }, 0);
        }
        const focusId = this._focusAfterRender || modalFocusId;
        const selection = focusId === modalFocusId ? modalFocusSelection : null;
        this._focusAfterRender = null;
        if (this._scrollToOta) {
          this._scrollToOta = false;
          const otaAnchor = this.shadowRoot.getElementById("ota-section");
          if (otaAnchor && typeof otaAnchor.scrollIntoView === "function") {
            otaAnchor.scrollIntoView({ behavior: "auto", block: "start" });
          }
        }
        if (focusId) {
          const focusEl = this.shadowRoot.getElementById(focusId);
          if (focusEl && typeof focusEl.focus === "function") {
            try {
              focusEl.focus({ preventScroll: true });
            } catch (err) {
              focusEl.focus();
            }
            if (selection && typeof focusEl.setSelectionRange === "function") {
              focusEl.setSelectionRange(selection.start, selection.end);
            }
          }
        }
      });
    }
  }
}

customElements.define(CARD_TAG, WaterTankCard);
console.info(`%c${CARD_TAG} v${VERSION} loaded`, "color: #03a9f4;");
