/* water-tank-card.js
 * Custom Home Assistant Lovelace card (no build step).
 * v0.4.7: calibration UX feedback + theme-safe modal inputs.
 */

const CARD_TAG = "water-tank-card";
const VERSION = "0.4.7";

class WaterTankCard extends HTMLElement {
  constructor() {
    super();
    this.attachShadow({ mode: "open" });
    this._config = null;
    this._hass = null;
    this._modalOpen = false;
    this._modalPage = "main";
    this._pendingSimMode = null;
    this._pendingSimUntil = 0;
    this._focusAfterRender = null;
    this._toast = { open: false, text: "", type: "info" };
    this._toastTimer = null;
    this._pendingSet = {};
    this._activeEditKey = null;
    this._modalPointerHandler = null;
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
    const required = [
      "percent_entity",
      "liters_entity",
      "cm_entity",
      "status_entity",
      "probe_entity",
      "percent_valid_entity",
      "calibration_entity",
      "raw_entity",
    ];

    for (const key of required) {
      if (!config[key]) throw new Error(`Missing required config key: ${key}`);
    }

    // Optional entities for modal controls (strongly recommended)
    this._config = {
      title: "Water Tank",
      // modal entities (optional)
      tank_volume_entity: null,
      rod_length_entity: null,
      calibrate_dry_entity: null,
      calibrate_wet_entity: null,
      clear_calibration_entity: null,

      // simulation controls (optional)
      simulation_enabled_entity: null,
      simulation_mode_entity: null,

      // optional calibration value readouts
      cal_dry_value_entity: null,
      cal_wet_value_entity: null,
      cal_dry_set_entity: null,
      cal_wet_set_entity: null,

      // optional diagnostics
      quality_reason_entity: null,

      ...config,
    };

    this._render();
  }

  set hass(hass) {
    this._hass = hass;
    this._checkPendingSets();
    this._render();
  }

  getCardSize() {
    return 3;
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

  _isUnknownState(s) {
    return s === null || s === undefined || s === "unknown" || s === "unavailable";
  }

  _safeText(v, fallback = "—") {
    if (v === null || v === undefined) return fallback;
    if (typeof v === "number" && !Number.isFinite(v)) return fallback;
    const s = String(v);
    return s.length ? s : fallback;
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
      console.warn(`${CARD_TAG}: service call failed`, domain, service, err);
    }
  }

  _showToast(text, type = "info", ms = 2200) {
    if (!text) return;
    this._toast = { open: true, text, type };
    if (this._toastTimer) clearTimeout(this._toastTimer);
    if (ms > 0) {
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

  _checkPendingSets() {
    if (!this._pendingSet) return;
    const keys = Object.keys(this._pendingSet);
    if (!keys.length) return;
    const now = Date.now();
    keys.forEach((key) => {
      const pending = this._pendingSet[key];
      if (!pending) return;
      const current = this._state(pending.entityId);
      const matches = this._valuesMatch(current, pending.targetValue);
      const expired = now - pending.startedAt > 5000;
      if (matches) {
        delete this._pendingSet[key];
        this._editing[key] = false;
        if (this._activeEditKey === key) this._activeEditKey = null;
        if (!this._isUnknownState(current)) this._draft[key] = current;
        if (this._modalOpen) this._showToast("Saved", "success");
      } else if (expired) {
        delete this._pendingSet[key];
        if (this._modalOpen) this._showToast("Save failed (no update)", "error");
      }
    });
  }

  _setNumber(entityId, value) {
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
      this._showToast("Service failed", "error");
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
    const entityId = this._getCalSetEntityId(draftKey);
    if (!entityId) return false;
    const domain = this._domain(entityId);
    if (domain !== "number" && domain !== "input_number") return false;
    const raw = this._draft[draftKey];
    const trimmed = raw !== null && raw !== undefined ? String(raw).trim() : "";
    if (!trimmed || !this._isNumberLike(trimmed)) {
      this._showToast("Enter a valid number", "warn");
      return false;
    }
    const numVal = Number(trimmed);
    if (!this._setNumber(entityId, numVal)) return false;
    const startedAt = Date.now();
    this._pendingSet[draftKey] = { entityId, targetValue: numVal, startedAt };
    this._showToast("Saving...", "info");
    setTimeout(() => {
      const p = this._pendingSet?.[draftKey];
      if (!p || p.startedAt !== startedAt) return;

      this._checkPendingSets();

      // if still pending after check, force a failure toast + cleanup
      if (this._pendingSet?.[draftKey]?.startedAt === startedAt) {
        delete this._pendingSet[draftKey];
        this._editing[draftKey] = false;
        if (this._activeEditKey === draftKey) this._activeEditKey = null;
        this._showToast("Save failed (no update)", "error");
      }

      if (this._modalOpen) this._render();
    }, 5200);
    return true;
  }

  _commitNumberEdit(draftKey, entityId, rawValue, opts = {}) {
    if (!draftKey || !entityId || !this._hass) return false;
    if (this._pendingSet?.[draftKey]) return false;

    const label = opts.label || "Saved";

    const trimmed = rawValue !== null && rawValue !== undefined ? String(rawValue).trim() : "";
    if (!trimmed || !this._isNumberLike(trimmed)) {
      this._showToast("Enter a valid number", "warn");
      return false;
    }

    const numVal = Number(trimmed);
    if (!(numVal > 0)) {
      this._showToast("Enter a number greater than 0", "warn");
      return false;
    }

    const current = this._num(entityId);
    const unchanged = current !== null && Math.abs(current - numVal) < 0.001;
    if (unchanged) return false;

    if (!this._setNumber(entityId, numVal)) {
      this._showToast("Service failed", "error");
      return false;
    }

    const startedAt = Date.now();
    this._pendingSet[draftKey] = { entityId, targetValue: numVal, startedAt, label };
    this._showToast("Saving...", "info");

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
    const entityId = draftKey === "dryValue" ? this._config.cal_dry_value_entity : this._config.cal_wet_value_entity;
    const current = entityId ? this._state(entityId) : null;
    if (!this._isUnknownState(current)) this._draft[draftKey] = current;
    this._editing[draftKey] = false;
    if (this._activeEditKey === draftKey) this._activeEditKey = null;
    this._showToast("Canceled", "info");
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

  _collectWarnings() {
    const statusState = this._state(this._config.status_entity);
    const probeState = this._state(this._config.probe_entity);
    const calState = this._state(this._config.calibration_entity);
    const percentValidState = this._state(this._config.percent_valid_entity);

    const warnings = [];
    if (this._isUnknownState(statusState) || statusState !== "online") warnings.push({ icon: "mdi:lan-disconnect", text: "Device offline or status unknown" });
    if (probeState === "off") warnings.push({ icon: "mdi:power-plug-off-outline", text: "Probe disconnected" });
    else if (this._isUnknownState(probeState)) warnings.push({ icon: "mdi:help-circle-outline", text: "Probe state unknown" });

    if (calState === "needs_calibration") warnings.push({ icon: "mdi:ruler", text: "Needs calibration" });
    else if (calState === "calibrating") warnings.push({ icon: "mdi:cog", text: "Calibrating…" });

    if (percentValidState !== "on") warnings.push({ icon: "mdi:alert-outline", text: "Readings not valid" });

    if (this._config.quality_reason_entity) {
      const qr = this._state(this._config.quality_reason_entity);
      if (!this._isUnknownState(qr) && qr && qr !== "ok") {
        warnings.push({ icon: "mdi:waveform", text: `Quality: ${qr}` });
      }
    }

    return warnings;
  }

  _setDraftFromEntitiesIfEmpty() {
    const setIfEmpty = (key, entityId) => {
      if (!entityId) return;
      if (this._draft[key] !== "") return;
      const v = this._state(entityId);
      if (!this._isUnknownState(v)) this._draft[key] = v ?? "";
    };

    setIfEmpty("tankVolume", this._config.tank_volume_entity);
    setIfEmpty("rodLength", this._config.rod_length_entity);
    setIfEmpty("dryValue", this._config.cal_dry_value_entity);
    setIfEmpty("wetValue", this._config.cal_wet_value_entity);
  }

  _openModal(page = "main") {
    this._modalOpen = true;
    this._modalPage = page;
    this._setDraftFromEntitiesIfEmpty();
    this._render();
  }

  _closeModal() {
    this._modalOpen = false;
    this._activeEditKey = null;
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

    const offline = this._isUnknownState(status) || status === "offline";
    if (offline) return { display: "nullify", severity: "error", icon: "mdi:lan-disconnect", msg: "Device offline" };

    if (probeState === "off") return { display: "nullify", severity: "error", icon: "mdi:power-plug-off-outline", msg: "Probe disconnected" };

    if (this._isUnknownState(probeState)) return { display: "nullify", severity: "warn", icon: "mdi:help-circle-outline", msg: "Probe status unknown" };

    if (calState === "calibrating") return { display: "dim", severity: "warn", icon: "mdi:cog", msg: "Calibrating…" };

    if (calState === "needs_calibration") return { display: "dim", severity: "warn", icon: "mdi:ruler", msg: "Needs calibration" };

    if (percentValidState !== "on") return { display: "dim", severity: "warn", icon: "mdi:alert-outline", msg: "Readings not valid" };

    return { display: "normal", severity: "ok", icon: "mdi:check-circle-outline", msg: "All readings valid" };
  }

  // ---------- modal (in-card) ----------
  _renderModalHtml() {
    if (!this._modalOpen) return "";

    const statusState = this._state(this._config.status_entity);
    const online = statusState === "online";
    const badge = this._buildOnlineBadgeHtml(online);
    const warnings = this._collectWarnings();

    const warningsHtml = warnings.length
      ? `<ul class="wt-warning-list">${warnings
        .map((w) => `<li><ha-icon icon="${w.icon}"></ha-icon><span>${this._safeText(w.text)}</span></li>`)
        .join("")}</ul>`
      : `<div class="wt-all-good">✅ All good — no issues detected</div>`;

    const probeState = this._state(this._config.probe_entity);
    const probeDisconnected = probeState === "off";
    const probeUnknown = this._isUnknownState(probeState);
    const toastHtml = this._toast?.open && this._toast.text
      ? `<div class="wt-toast wt-toast--${this._toast.type}">${this._safeText(this._toast.text, "")}</div>`
      : "";

    const renderCalValue = (key, label, entityId, draftKey) => {
      if (!entityId) return `<div class="wt-cal-value"><div class="wt-cal-label">${label}</div><div class="wt-cal-read">—</div></div>`;
      const rawValue = this._state(entityId);
      const displayValue = this._safeText(rawValue);
      const setEntityId = this._getCalSetEntityId(draftKey) || entityId;
      const setDomain = this._domain(setEntityId);
      const editable = !!setEntityId && (setDomain === "number" || setDomain === "input_number");
      const isPending = !!this._pendingSet?.[draftKey];
      const isEditing = !!this._editing[draftKey];
      if (!editable || !isEditing) {
        return `
          <div class="wt-cal-value">
            <div class="wt-cal-label">${label}</div>
            <div class="wt-cal-input-row">
              <button class="wt-cal-edit" id="wt-${key}-edit" data-entity="${entityId}" type="button" ${editable ? "" : "disabled"}>
                <span class="wt-cal-edit-value">${displayValue}</span>
                <span class="wt-cal-edit-hint">${editable ? "Edit" : "Read-only"}</span>
              </button>
            </div>
          </div>`;
      }
      return `
          <div class="wt-cal-value">
            <div class="wt-cal-label">${label}</div>
            <div class="wt-cal-input-row">
              <input id="wt-${key}-input" type="text" value="${this._safeText(this._draft[draftKey], "")}" />
              <button class="wt-mini-btn" id="wt-${key}-set" data-entity="${entityId}" ${editable && !isPending ? "" : "disabled"}>Set</button>
            </div>
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
      ${toastHtml}
      <div class="wt-warnings">${warningsHtml}</div>
      ${calibrationSection}
      ${setupSection}
      <div class="wt-modal-footer">
        <button class="wt-link wt-modal-advanced"><ha-icon icon="mdi:chevron-right"></ha-icon><span>Advanced</span></button>
      </div>
    `;

    const simEnabledState = this._config.simulation_enabled_entity ? this._isOn(this._config.simulation_enabled_entity) : false;
    const simModeEntity = this._config.simulation_mode_entity;
    const simModeState = simModeEntity ? this._state(simModeEntity) : "";
    const simNow = Date.now();
    if (this._pendingSimMode !== null) {
      const matches = String(simModeState) === String(this._pendingSimMode);
      const expired = simNow > this._pendingSimUntil;
      if (matches || expired) {
        this._pendingSimMode = null;
        this._pendingSimUntil = 0;
        this._editing.simMode = false;
        this._draft.simMode = simModeState || "";
      }
    }
    const simModeSelected =
      this._pendingSimMode !== null
        ? this._pendingSimMode
        : (this._editing.simMode ? this._draft.simMode : simModeState);
    const simOptions = this._getOptions(simModeEntity);

    const diagnosticsLines = [
      { label: "Raw", value: this._safeText(this._state(this._config.raw_entity)) },
      { label: "Status", value: this._safeText(statusState) },
      { label: "Probe", value: this._safeText(probeState) },
      { label: "Calibration", value: this._safeText(this._state(this._config.calibration_entity)) },
      { label: "Percent valid", value: this._safeText(this._state(this._config.percent_valid_entity)) },
    ];
    if (this._config.quality_reason_entity) diagnosticsLines.push({ label: "Quality reason", value: this._safeText(this._state(this._config.quality_reason_entity)) });
    if (this._config.percent_entity) diagnosticsLines.push({ label: "Percent", value: this._safeText(this._state(this._config.percent_entity)) });
    if (this._config.liters_entity) diagnosticsLines.push({ label: "Liters", value: this._safeText(this._state(this._config.liters_entity)) });
    if (this._config.cm_entity) diagnosticsLines.push({ label: "Height", value: this._safeText(this._state(this._config.cm_entity)) });

    const diagnosticsHtml = diagnosticsLines
      .map((l) => `<div class="wt-diag-row"><span>${l.label}</span><b>${l.value}</b></div>`)
      .join("");

    const advancedPage = `
      <div class="wt-modal-header">
        <button class="wt-link wt-modal-back"><ha-icon icon="mdi:chevron-left"></ha-icon><span>Back</span></button>
        <div class="wt-modal-actions">
          ${badge}
          <button class="wt-modal-close" aria-label="Close">✕</button>
        </div>
      </div>
      ${toastHtml}
      <div class="wt-section">
        <div class="wt-section-title">Advanced</div>
      </div>
      ${(this._config.simulation_enabled_entity || this._config.simulation_mode_entity) ? `
      <div class="wt-section">
        <div class="wt-section-sub" style="margin-bottom:8px;">Simulation controls</div>
        ${this._config.simulation_enabled_entity
          ? `<div class="wt-setup-row">
            <div class="wt-setup-icon"><ha-icon icon="mdi:toggle-switch"></ha-icon></div>
            <div class="wt-setup-body">
              <div class="wt-setup-label">Simulation enabled</div>
              <div class="wt-setup-input">
                <button class="wt-btn ${simEnabledState ? "on" : ""}" id="simToggleBtn" data-entity="${this._config.simulation_enabled_entity}" data-state="${simEnabledState ? "on" : "off"}">${simEnabledState ? "On" : "Off"}</button>
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
    `;

    const content = this._modalPage === "advanced" ? advancedPage : mainPage;

    return `
      <div class="wt-modal-overlay">
        <div class="wt-modal-card">
          ${content}
        </div>
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
        this._showToast("Probe disconnected", "error");
        return;
      }
      if (!entityId) return;
      this._showToast(`${label} captured...`, "info");
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
      const seedEntityId =
        displayEntityId ||
        (draftKey === "dryValue" ? this._config.cal_dry_value_entity : this._config.cal_wet_value_entity);
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
        this._showToast("Canceled", "info");
        validate();
      };

      const commit = () => {
        const { valid, unchanged } = validate();
        if (!valid || unchanged) return;
        this._editing[draftKey] = true;
        this._activeEditKey = draftKey;
        this._commitNumberEdit(draftKey, entityId, input.value, { label: "Saved" });
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
        const entityId = simToggle.dataset.entity;
        if (!entityId) return;
        const current = simToggle.dataset.state === "on";
        const domain = this._domain(entityId);
        if (!domain) return;
        this._callService(domain, current ? "turn_off" : "turn_on", { entity_id: entityId });
      };
    }

    const simMode = sr.getElementById("simModeSelect");
    if (simMode) {
      simMode.addEventListener("focus", (e) => {
        e.stopPropagation();
        if (!this._editing.simMode) {
          this._draft.simMode = simMode.value;
        }
        this._editing.simMode = true;
      });
      simMode.onchange = (e) => {
        e.preventDefault();
        e.stopPropagation();
        const entityId = simMode.dataset.entity;
        if (!entityId) return;
        const selected = simMode.value;
        this._editing.simMode = true;
        this._pendingSimMode = selected;
        this._pendingSimUntil = Date.now() + 5000;
        this._draft.simMode = selected;
        const domain = this._domain(entityId);
        if (domain === "select") {
          this._callService("select", "select_option", { entity_id: entityId, option: selected });
        } else if (domain === "input_select") {
          this._callService("input_select", "select_option", { entity_id: entityId, option: selected });
        }
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
    if (!this._config || !this._hass) return;

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
    const onlineText = statusState === "online" ? "Online" : "Offline";

    // Simulation mode enabled?
    const simEnabled =
      !!this._config.simulation_enabled_entity &&
      this._isOn(this._config.simulation_enabled_entity);
    const probeState = this._state(this._config.probe_entity);

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
        margin-bottom: 10px;
        background: var(--secondary-background-color, rgba(0,0,0,0.06));
        border: 1px solid var(--divider-color, rgba(127,127,127,0.35));
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

    const pctText = nullify ? "—%" : (pct !== null ? `${pct.toFixed(1)}%` : "—%");
    const litersText = nullify ? "—" : this._safeText(liters !== null ? liters.toFixed(2) : null);
    const cmText = nullify ? "—" : this._safeText(cm !== null ? cm.toFixed(1) : null);

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
              <div><b>${litersText}</b> L</div>
            </div>

            <div class="metricRow">
              <ha-icon icon="mdi:ruler"></ha-icon>
              <div><b>${cmText}</b> cm</div>
            </div>
          </div>
        </div>

        <div class="rawLine">Raw: ${this._safeText(raw)}</div>

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

    const settingButtons = this.shadowRoot.querySelectorAll("#settingsBtn");
    settingButtons.forEach((btn) => {
      btn.onclick = () => this._openModal("main");
    });

    const simOff = this.shadowRoot.getElementById("simOffBtn");
    if (simOff && this._config.simulation_enabled_entity) {
      simOff.onclick = (e) => {
        e.stopPropagation();
        this._hass.callService("switch", "turn_off", {
          entity_id: this._config.simulation_enabled_entity,
        });
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
