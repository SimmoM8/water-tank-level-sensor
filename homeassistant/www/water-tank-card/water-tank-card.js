/* water-tank-card.js
 * Custom Home Assistant Lovelace card (no build step).
 * v0.4.1: arc gauge + error-only view + warning footer + in-card settings modal.
 */

const CARD_TAG = "water-tank-card";
const VERSION = "0.4.3";

class WaterTankCard extends HTMLElement {
  constructor() {
    super();
    this.attachShadow({ mode: "open" });
    this._config = null;
    this._hass = null;
    this._modalOpen = false;
    this._modalPage = "main";
    this._draft = {
      tankVolume: "",
      rodLength: "",
      dryValue: "",
      wetValue: "",
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

      // optional diagnostics
      quality_reason_entity: null,

      ...config,
    };

    this._render();
  }

  set hass(hass) {
    this._hass = hass;
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

    const renderCalValue = (key, label, entityId, draftKey) => {
      if (!entityId) return `<div class="wt-cal-value"><div class="wt-cal-label">${label}</div><div class="wt-cal-read">—</div></div>`;
      const val = this._safeText(this._state(entityId));
      return `
          <div class="wt-cal-value">
            <div class="wt-cal-label">${label}</div>
            <div class="wt-cal-input-row">
              <input id="wt-${key}-input" type="text" value="${this._safeText(this._draft[draftKey] || val, "")}" />
              <button class="wt-mini-btn" id="wt-${key}-set" data-entity="${entityId}">Set</button>
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

    const tankRow = this._config.tank_volume_entity
      ? `
        <div class="wt-setup-row">
          <div class="wt-setup-icon"><ha-icon icon="mdi:water"></ha-icon></div>
          <div class="wt-setup-body">
            <div class="wt-setup-label">Tank volume</div>
            <div class="wt-setup-input">
              <input id="tankVolumeInput" type="text" value="${this._safeText(this._draft.tankVolume || this._state(this._config.tank_volume_entity), "")}" />
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
              <input id="rodLengthInput" type="text" value="${this._safeText(this._draft.rodLength || this._state(this._config.rod_length_entity), "")}" />
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

    const simEnabledState = this._config.simulation_enabled_entity ? this._isOn(this._config.simulation_enabled_entity) : false;
    const simModeEntity = this._config.simulation_mode_entity;
    const simModeState = simModeEntity ? this._state(simModeEntity) : "";
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
                  ${simOptions.map((opt) => `<option value="${opt}" ${String(opt) === String(simModeState) ? "selected" : ""}>${opt}</option>`).join("")}
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
      <div class="wt-modal-footer">
        <button class="wt-link wt-modal-back"><ha-icon icon="mdi:chevron-left"></ha-icon><span>Back</span></button>
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

    const removeEsc = () => {
      if (this._modalEscHandler) window.removeEventListener("keydown", this._modalEscHandler);
    };

    if (!this._modalOpen) {
      removeEsc();
      return;
    }

    if (!this._modalEscHandler) {
      this._modalEscHandler = (e) => {
        if (e.key === "Escape") this._closeModal();
      };
    }
    window.addEventListener("keydown", this._modalEscHandler);

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

    // Calibration buttons
    const dryBtn = sr.getElementById("btnCalDry");
    if (dryBtn && !dryBtn.classList.contains("disabled")) {
      dryBtn.onclick = (e) => {
        e.preventDefault();
        e.stopPropagation();
        pressButton(dryBtn.dataset.entity);
      };
    }
    const wetBtn = sr.getElementById("btnCalWet");
    if (wetBtn && !wetBtn.classList.contains("disabled")) {
      wetBtn.onclick = (e) => {
        e.preventDefault();
        e.stopPropagation();
        pressButton(wetBtn.dataset.entity);
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

    // Calibration value setters (optional)
    const setNumber = (entityId, value) => {
      if (!entityId || value === undefined) return;
      const domain = this._domain(entityId);
      this._callService(domain, "set_value", { entity_id: entityId, value });
    };

    const updateDraft = (key, inputId) => {
      const el = sr.getElementById(inputId);
      if (!el) return null;
      el.addEventListener("input", (e) => {
        e.stopPropagation();
        this._draft[key] = e.target.value;
      });
      return el;
    };

    const dryInput = updateDraft("dryValue", "wt-dry-input");
    const wetInput = updateDraft("wetValue", "wt-wet-input");
    const drySet = sr.getElementById("wt-dry-set");
    if (drySet) {
      drySet.onclick = (e) => {
        e.preventDefault();
        e.stopPropagation();
        setNumber(drySet.dataset.entity, dryInput ? dryInput.value : undefined);
      };
    }
    const wetSet = sr.getElementById("wt-wet-set");
    if (wetSet) {
      wetSet.onclick = (e) => {
        e.preventDefault();
        e.stopPropagation();
        setNumber(wetSet.dataset.entity, wetInput ? wetInput.value : undefined);
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

      const validate = () => {
        const raw = input.value?.trim() ?? "";
        const valid = raw.length > 0 && this._isNumberLike(raw) && Number(raw) > 0;
        const numVal = valid ? Number(raw) : NaN;
        const current = this._num(entityId);
        const unchanged = valid && current !== null && Math.abs(current - numVal) < 0.001;
        setError(valid ? "" : "Enter a number greater than 0");
        save.disabled = !valid || unchanged;
        return { valid, numVal, unchanged };
      };

      input.addEventListener("input", (e) => {
        e.stopPropagation();
        this._draft[draftKey] = input.value;
        validate();
      });

      save.addEventListener("click", (e) => {
        e.preventDefault();
        e.stopPropagation();
        const { valid, numVal, unchanged } = validate();
        if (!valid || unchanged) return;
        this._callService("number", "set_value", { entity_id: entityId, value: numVal });
        const original = save.textContent;
        save.textContent = "Saved";
        save.disabled = true;
        setTimeout(() => {
          save.textContent = original;
          validate();
        }, 1200);
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
        this._callService("switch", current ? "turn_off" : "turn_on", { entity_id: entityId });
      };
    }

    const simMode = sr.getElementById("simModeSelect");
    if (simMode) {
      simMode.onchange = (e) => {
        e.preventDefault();
        e.stopPropagation();
        const entityId = simMode.dataset.entity;
        if (!entityId) return;
        this._callService("select", "select_option", { entity_id: entityId, option: simMode.value });
      };
    }
  }

  // ---------- tank svg ----------
  _renderTankSvg(percent, ui) {
    const p = this._clamp(percent ?? 0, 0, 100);

    // Layout sizing
    const w = 190;
    const h = 160;

    // Inner tank area (where water can appear)
    const pad = 14;
    const innerX = pad;
    const innerY = pad + 10;
    const innerW = w - pad * 2;
    const innerH = h - (pad * 2) - 18;

    const fillH = Math.round((innerH * p) / 100);
    const fillY = innerY + (innerH - fillH);

    // Use the existing state model
    const nullify = ui?.display === "nullify";
    const showFill = !nullify;

    // Corner radii (tighter = more tank-like / industrial)
    const outerR = 10;
    const innerR = 8;

    // A) Lid / manhole cap
    const lidW = 44;
    const lidH = 10;
    const lidX = (w - lidW) / 2;
    const lidY = pad - 3;

    // B) Vertical ribs
    const ribCount = 7;
    const ribs = Array.from({ length: ribCount }, (_, i) => {
      const x = innerX + Math.round(((i + 1) * innerW) / (ribCount + 1));
      return `<line class="tankRib" x1="${x}" y1="${innerY + 4}" x2="${x}" y2="${innerY + innerH - 4}" />`;
    }).join("");

    // D) Level ticks (0/25/50/75/100)
    const tickX1 = innerX - 10;
    const tickX2 = innerX - 2;
    const tickBoldX2 = innerX + 2;
    const tickCount = 5;
    const ticks = Array.from({ length: tickCount }, (_, i) => {
      const t = i / (tickCount - 1); // 0..1
      const y = Math.round(innerY + t * innerH);
      const bold = i === 0 || i === tickCount - 1 || i === 2; // 0, 50, 100
      const x2 = bold ? tickBoldX2 : tickX2;
      return `<line class="tankTick${bold ? " bold" : ""}" x1="${tickX1}" y1="${y}" x2="${x2}" y2="${y}" />`;
    }).join("");

    // C) Outlet pipe + valve (right side near bottom)
    const pipeH = 12;
    const pipeW = 22;
    const pipeY = Math.round(innerY + innerH - 28);
    const pipeX = w - pad;
    const valveCx = pipeX + pipeW + 10;
    const valveCy = pipeY + pipeH / 2;
    const valveR = 6;

    return `
      <svg class="tankSvg" width="${w}" height="${h}" viewBox="0 0 ${w} ${h}" aria-label="Water tank level">
        <defs>
          <linearGradient id="wtFillGrad" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stop-color="var(--primary-color)" stop-opacity="0.85"></stop>
            <stop offset="100%" stop-color="var(--primary-color)" stop-opacity="0.35"></stop>
          </linearGradient>

          <clipPath id="wtTankClip">
            <rect x="${innerX}" y="${innerY}" width="${innerW}" height="${innerH}" rx="${innerR}" ry="${innerR}"></rect>
          </clipPath>
        </defs>

        <!-- A) Lid / manhole cap -->
        <rect class="tankLid" x="${lidX}" y="${lidY}" width="${lidW}" height="${lidH}" rx="6" ry="6"></rect>
        <line class="tankLidDetail" x1="${lidX + 10}" y1="${lidY + lidH / 2}" x2="${lidX + lidW - 10}" y2="${lidY + lidH / 2}" />

        <!-- Outer glass / outline -->
        <rect class="tankOutline"
              x="${pad}" y="${pad}"
              width="${w - pad * 2}" height="${h - pad * 2}"
              rx="${outerR}" ry="${outerR}"></rect>

        <!-- D) Level ticks -->
        <g class="tankTicks">${ticks}</g>

        <!-- Inner cavity -->
        <rect class="tankInner"
              x="${innerX}" y="${innerY}"
              width="${innerW}" height="${innerH}"
              rx="${innerR}" ry="${innerR}"></rect>

        <!-- B) Ribs -->
        <g class="tankRibs">${ribs}</g>

        <!-- Fill -->
        <g clip-path="url(#wtTankClip)">
          ${showFill
        ? `<rect class="tankFill" x="${innerX}" y="${fillY}" width="${innerW}" height="${fillH}" fill="url(#wtFillGrad)"></rect>`
        : ``
      }
          ${showFill && fillH > 0
        ? `<path class="tankSurface" d="M ${innerX} ${fillY} H ${innerX + innerW}" />`
        : ``
      }
        </g>

        <!-- C) Outlet pipe + valve -->
        <rect class="tankPipe" x="${pipeX}" y="${pipeY}" width="${pipeW}" height="${pipeH}" rx="4" ry="4"></rect>
        <circle class="tankValve" cx="${valveCx}" cy="${valveCy}" r="${valveR}"></circle>
        <line class="tankValveHandle" x1="${valveCx - 8}" y1="${valveCy - 10}" x2="${valveCx + 8}" y2="${valveCy - 10}" />
        <line class="tankValveHandle" x1="${valveCx}" y1="${valveCy - 10}" x2="${valveCx}" y2="${valveCy - 4}" />
      </svg>
    `;
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
      }

      .tankOutline {
        fill: rgba(255,255,255,0.02);
        stroke: rgba(255,255,255,0.18);
        stroke-width: 2;
      }

      .tankInner {
        fill: rgba(0,0,0,0.08);
        stroke: rgba(255,255,255,0.06);
        stroke-width: 1;
      }

      .tankFill {
        transition: all 200ms ease;
      }

      .tankSurface {
        stroke: rgba(255,255,255,0.35);
        stroke-width: 2;
        opacity: 0.6;
      }

            .tankLid {
        fill: rgba(255,255,255,0.04);
        stroke: rgba(255,255,255,0.22);
        stroke-width: 2;
      }

      .tankLidDetail {
        stroke: rgba(255,255,255,0.22);
        stroke-width: 2;
        stroke-linecap: round;
        opacity: 0.8;
      }

      .tankRib {
        stroke: rgba(255,255,255,0.10);
        stroke-width: 1;
      }

      .tankTick {
        stroke: rgba(255,255,255,0.14);
        stroke-width: 1;
        stroke-linecap: round;
      }

      .tankTick.bold {
        stroke: rgba(255,255,255,0.22);
        stroke-width: 2;
      }

      .tankPipe {
        fill: rgba(255,255,255,0.03);
        stroke: rgba(255,255,255,0.18);
        stroke-width: 2;
      }

      .tankValve {
        fill: rgba(0,0,0,0.06);
        stroke: rgba(255,255,255,0.20);
        stroke-width: 2;
      }

      .tankValveHandle {
        stroke: rgba(255,255,255,0.20);
        stroke-width: 2;
        stroke-linecap: round;
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
      .wt-cal-input-row input,
      .wt-setup-input input,
      select#wt-sim-mode {
        flex: 1;
        border-radius: 8px;
        border: 1px solid rgba(0,0,0,0.2);
        padding: 8px;
        font-size: 14px;
        background: var(--input-background-color, #fff);
        color: var(--primary-text-color, #111);
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
  }
}

customElements.define(CARD_TAG, WaterTankCard);
console.info(`%c${CARD_TAG} v${VERSION} loaded`, "color: #03a9f4;");
