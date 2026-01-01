/* water-tank-card.js
 * Custom Home Assistant Lovelace card (no build step).
 * v0.3: arc gauge + error-only view + warning footer + Browser Mod settings modal.
 */

const CARD_TAG = "water-tank-card";
const VERSION = "0.3.9";

class WaterTankCard extends HTMLElement {
    constructor() {
        super();
        this.attachShadow({ mode: "open" });
        this._config = null;
        this._hass = null;
        this._settingsPage = "main";
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

    // ---------- state model ----------
    _computeUiState() {
        const status = this._state(this._config.status_entity); // "online" expected
        const offline = this._isUnknownState(status) || status === "offline";

        const probeState = this._state(this._config.probe_entity); // on/off/unknown
        const percentValidState = this._state(this._config.percent_valid_entity); // on/off/unknown
        const calState = this._state(this._config.calibration_entity); // needs_calibration | calibrating | calibrated

        const probeUnknown = this._isUnknownState(probeState);
        const probeConnected = probeState === "on";
        const probeDisconnected = probeState === "off";

        const percentValid = percentValidState === "on";
        const percentValidUnknown = this._isUnknownState(percentValidState);

        // 1) Offline overrides everything
        if (offline) return { mode: "OFFLINE", msg: "Device offline", status };

        // 2) Probe disconnected → error-only view (no values shown)
        if (probeDisconnected) return { mode: "ERROR", msg: "Probe disconnected", status };

        // 3) Probe status unknown → setup (not error) so it doesn’t look “broken”
        if (probeUnknown) return { mode: "SETUP", msg: "Waiting for probe data…", status };

        // 4) Calibration / validity
        if (!percentValid || calState === "needs_calibration" || percentValidUnknown) {
            const m =
                calState === "calibrating"
                    ? "Calibrating…"
                    : calState === "needs_calibration"
                        ? "Needs calibration"
                        : "Readings not valid";
            return { mode: "SETUP", msg: m, status };
        }

        // 5) OK
        return { mode: "OK", msg: "OK", status };
    }

    // ---------- modal ----------
    _buildPopupData(page = "main", setPage = true) {
        if (setPage) this._settingsPage = page;
        const cards = page === "advanced" ? this._buildSettingsAdvancedCards() : this._buildSettingsMainCards();
        return {
            title: `${this._config.title || "Water Tank"} Settings`,
            content: {
                type: "vertical-stack",
                cards,
            },
        };
    }

    _openSettingsModal(page = "main") {
        const hasBrowserMod = !!this._hass?.services?.browser_mod?.popup;
        if (hasBrowserMod) {
            this._hass.callService("browser_mod", "popup", this._buildPopupData(page, true));
            setTimeout(() => {
                const root = document.querySelector('home-assistant')?.shadowRoot
                    ?.querySelector('hui-root')?.shadowRoot;
                if (!root) return;
                root.addEventListener('water-tank-advanced', () => this._openSettingsModal('advanced'), { once: true });
                root.addEventListener('water-tank-main', () => this._openSettingsModal('main'), { once: true });
            }, 0);
            return;
        }

        // Fallback: open HA More Info for raw entity
        this.dispatchEvent(
            new CustomEvent("hass-more-info", {
                bubbles: true,
                composed: true,
                detail: { entityId: this._config.raw_entity },
            })
        );
    }

    _openAdvanced() {
        this._openSettingsModal("advanced");
    }

    _openMain() {
        this._openSettingsModal("main");
    }

    _buildOnlineBadgeHtml(online) {
      const bg = online ? "rgba(0,150,0,0.12)" : "rgba(255,0,0,0.12)";
      const br = online ? "rgba(0,150,0,0.35)" : "rgba(255,0,0,0.35)";
      return `<span style="padding:3px 9px;border-radius:999px;font-weight:800;font-size:11px;background:${bg};border:1px solid ${br};">${online ? "Online" : "Offline"}</span>`;
    }

    _buildSettingsMainCards() {
      const statusState = this._state(this._config.status_entity);
      const probeState = this._state(this._config.probe_entity);
      const calState = this._state(this._config.calibration_entity);
      const percentValidState = this._state(this._config.percent_valid_entity);

      const probeDisconnected = probeState === "off";
      const probeUnknown = this._isUnknownState(probeState);
      const online = statusState === "online";
      const badge = this._buildOnlineBadgeHtml(online);

      // Build warning list (human-friendly)
      const warnings = [];
      if (this._isUnknownState(statusState) || !online) warnings.push({ icon: "mdi:lan-disconnect", text: "Device offline or status unknown" });
      if (probeDisconnected) warnings.push({ icon: "mdi:power-plug-off-outline", text: "Probe disconnected" });
      else if (probeUnknown) warnings.push({ icon: "mdi:help-circle-outline", text: "Waiting for probe data" });

      if (calState === "needs_calibration") warnings.push({ icon: "mdi:ruler", text: "Needs calibration" });
      else if (calState === "calibrating") warnings.push({ icon: "mdi:cog", text: "Calibrating…" });

      if (percentValidState !== "on") warnings.push({ icon: "mdi:alert-outline", text: "Readings not valid" });

      if (this._config.quality_reason_entity) {
        const qr = this._state(this._config.quality_reason_entity);
        if (!this._isUnknownState(qr) && qr && qr !== "ok") {
          warnings.push({ icon: "mdi:waveform", text: `Quality: ${qr}` });
        }
      }

      const warningsHtml = warnings.length
        ? warnings
            .map((w) => `<div style="display:flex;gap:10px;align-items:flex-start;padding:6px 0;">
                <ha-icon icon="${w.icon}" style="opacity:0.85;"></ha-icon>
                <div style="font-size:14px;line-height:1.3;">${this._safeText(w.text)}</div>
              </div>`)
            .join("")
        : `<div style="display:flex;gap:10px;align-items:center;padding:8px 0;">
              <ha-icon icon="mdi:check-circle-outline" style="opacity:0.85;"></ha-icon>
              <div style="font-size:14px;font-weight:700;">All good — no issues detected</div>
           </div>`;

      const statusCard = {
        type: "markdown",
        content: `
<div style="display:flex;align-items:center;justify-content:space-between;gap:12px;">
  <div style="font-size:15px;font-weight:900;">${this._config.title || "Water Tank"}</div>
  <div>${badge}</div>
</div>
<div style="margin-top:10px;">${warningsHtml}</div>
`,
      };

      // ----- Calibration section -----
      const disableCal = probeDisconnected;

      const dryVal = this._config.cal_dry_value_entity ? this._safeText(this._state(this._config.cal_dry_value_entity)) : "—";
      const wetVal = this._config.cal_wet_value_entity ? this._safeText(this._state(this._config.cal_wet_value_entity)) : "—";

      const calHeaderRow = {
        type: "horizontal-stack",
        cards: [
          {
            type: "markdown",
            content: `
<div style="display:flex;flex-direction:column;gap:4px;">
  <div style="font-size:15px;font-weight:900;">Calibration</div>
  <div style="font-size:13px;opacity:0.8;">Calibrate the probe by setting its dry and fully submerged values.</div>
</div>
`,
          },
          this._config.clear_calibration_entity
            ? {
                type: "button",
                icon: "mdi:close-circle-outline",
                name: "",
                show_name: false,
                show_icon: true,
                tap_action: {
                  action: "call-service",
                  service: "button.press",
                  target: { entity_id: this._config.clear_calibration_entity },
                },
                hold_action: { action: "none" },
              }
            : { type: "markdown", content: "" },
        ],
      };

      const makeCalButton = (entityId, label, icon) => {
        if (!entityId) return null;
        return {
          type: "button",
          name: label,
          icon,
          show_icon: true,
          show_name: true,
          tap_action: disableCal
            ? { action: "none" }
            : { action: "call-service", service: "button.press", target: { entity_id: entityId } },
          hold_action: { action: "none" },
        };
      };

      const dryBtn = makeCalButton(this._config.calibrate_dry_entity, "Dry", "mdi:water-off-outline");
      const wetBtn = makeCalButton(this._config.calibrate_wet_entity, "Wet", "mdi:water");

      const calGridCards = [];
      if (dryBtn) calGridCards.push(dryBtn);
      if (wetBtn) calGridCards.push(wetBtn);

      // Under-button value + (optional) inline editable entity row if provided
      const dryBelow = this._config.cal_dry_value_entity
        ? { type: "entity", entity: this._config.cal_dry_value_entity, name: `Dry value (raw)`, icon: "mdi:water-off-outline" }
        : { type: "markdown", content: `<div style="font-size:13px;opacity:0.85;">Dry value: <b>${dryVal}</b></div>` };

      const wetBelow = this._config.cal_wet_value_entity
        ? { type: "entity", entity: this._config.cal_wet_value_entity, name: `Wet value (raw)`, icon: "mdi:water" }
        : { type: "markdown", content: `<div style="font-size:13px;opacity:0.85;">Wet value: <b>${wetVal}</b></div>` };

      const calButtonsRow = {
        type: "horizontal-stack",
        cards: [dryBtn || { type: "markdown", content: "" }, wetBtn || { type: "markdown", content: "" }],
      };

      const calValuesRow = {
        type: "horizontal-stack",
        cards: [dryBelow, wetBelow],
      };

      const calNote = disableCal
        ? {
            type: "markdown",
            content: `<div style="margin-top:6px;font-size:12px;opacity:0.75;">Probe disconnected — calibration buttons are disabled.</div>`,
          }
        : {
            type: "markdown",
            content: `<div style="margin-top:6px;font-size:12px;opacity:0.75;">Dry = probe in air • Wet = probe fully submerged</div>`,
          };

      const calibrationStack = {
        type: "vertical-stack",
        cards: [calHeaderRow, calButtonsRow, calValuesRow, calNote],
      };

      // ----- Setup (tank volume + rod length) -----
      const setupEntities = [];
      if (this._config.tank_volume_entity) setupEntities.push({ entity: this._config.tank_volume_entity, name: "Tank volume (L)", icon: "mdi:water" });
      if (this._config.rod_length_entity) setupEntities.push({ entity: this._config.rod_length_entity, name: "Rod length (cm)", icon: "mdi:ruler" });

      const setupCards = [];
      if (setupEntities.length) {
        setupCards.push({
          type: "markdown",
          content: `
<div style="margin-top:6px;font-size:15px;font-weight:900;">Tank setup</div>
<div style="font-size:13px;opacity:0.8;margin-top:4px;">Used to calculate liters and cm. Changes save automatically.</div>
`,
        });
        setupCards.push({
          type: "entities",
          entities: setupEntities,
          show_header_toggle: false,
        });
      }

      // ----- Advanced link (subtle) -----
      const advancedBtn = {
        type: "button",
        name: "Advanced",
        icon: "mdi:chevron-right",
        show_icon: true,
        show_name: true,
        tap_action: {
          action: "fire-dom-event",
          event_type: "water-tank-advanced",
        },
        hold_action: { action: "none" },
      };

      const cards = [statusCard, calibrationStack, ...setupCards, advancedBtn];
      return cards;
    }

    _buildSettingsAdvancedCards() {
      const cards = [];
      const statusState = this._state(this._config.status_entity);
      const online = statusState === "online";
      const badge = this._buildOnlineBadgeHtml(online);

      cards.push({
        type: "markdown",
        content: `
<div style="display:flex;align-items:center;justify-content:space-between;gap:12px;">
  <div style="font-size:15px;font-weight:900;">Advanced</div>
  <div>${badge}</div>
</div>
<div style="font-size:13px;opacity:0.8;margin-top:6px;">Simulation + diagnostics.</div>
`,
      });

      // Simulation controls (advanced only)
      const simEntities = [];
      if (this._config.simulation_enabled_entity) simEntities.push({ entity: this._config.simulation_enabled_entity, name: "Simulation enabled", icon: "mdi:toggle-switch" });
      if (this._config.simulation_mode_entity) simEntities.push({ entity: this._config.simulation_mode_entity, name: "Simulation mode", icon: "mdi:chart-line" });

      if (simEntities.length) {
        cards.push({
          type: "entities",
          entities: simEntities,
          show_header_toggle: false,
        });
        cards.push({
          type: "markdown",
          content: `<div style="font-size:12px;opacity:0.75;">Simulation is for testing only. Turn it off for real readings.</div>`,
        });
      }

      // Diagnostics (compact)
      const lines = [];
      lines.push(`Raw: <b>${this._safeText(this._state(this._config.raw_entity))}</b>`);
      lines.push(`Status: <b>${this._safeText(statusState)}</b>`);
      lines.push(`Probe: <b>${this._safeText(this._state(this._config.probe_entity))}</b>`);
      lines.push(`Calibration: <b>${this._safeText(this._state(this._config.calibration_entity))}</b>`);
      lines.push(`Percent valid: <b>${this._safeText(this._state(this._config.percent_valid_entity))}</b>`);
      if (this._config.quality_reason_entity) lines.push(`Quality reason: <b>${this._safeText(this._state(this._config.quality_reason_entity))}</b>`);

      // Optional display values
      if (this._config.percent_entity) lines.push(`Percent: <b>${this._safeText(this._state(this._config.percent_entity))}</b>`);
      if (this._config.liters_entity) lines.push(`Liters: <b>${this._safeText(this._state(this._config.liters_entity))}</b>`);
      if (this._config.cm_entity) lines.push(`Height: <b>${this._safeText(this._state(this._config.cm_entity))}</b>`);

      cards.push({
        type: "markdown",
        content: `
<div style="margin-top:4px;font-size:15px;font-weight:900;">Diagnostics</div>
<div style="margin-top:8px;display:flex;flex-direction:column;gap:6px;font-size:13px;opacity:0.92;">
  ${lines.map((l) => `<div>${l}</div>`).join("")}
</div>
`,
      });

      // Back (subtle)
      cards.push({
        type: "button",
        name: "Back",
        icon: "mdi:chevron-left",
        show_icon: true,
        show_name: true,
        tap_action: {
          action: "fire-dom-event",
          event_type: "water-tank-main",
        },
        hold_action: { action: "none" },
      });

      return cards;
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

        // Warning footer logic (only when NOT error-only)
        const calState = this._state(this._config.calibration_entity);
        const percentValid = this._isOn(this._config.percent_valid_entity);
        const probeState = this._state(this._config.probe_entity);

        let warningText = "";
        if (ui.mode !== "ERROR" && ui.mode !== "OFFLINE") {
            if (calState === "needs_calibration") warningText = "Calibration required";
            else if (calState === "calibrating") warningText = "Calibrating…";
            else if (!percentValid) warningText = "Readings not valid";
            else if (this._isUnknownState(probeState)) warningText = "Probe status unknown";
        }

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
    `;

        let bodyHtml = "";

        // ERROR-ONLY view: show only error message + icon (no values)
        if (ui.mode === "ERROR" || ui.mode === "OFFLINE") {
            bodyHtml = `
        <div class="notice error">
          <ha-icon icon="mdi:alert-circle-outline"></ha-icon>
          <div class="msg">${ui.msg}</div>
          <div style="margin-left:auto;">
            <button class="btn" id="settingsBtn" title="Settings">
              <ha-icon icon="mdi:cog"></ha-icon>
            </button>
          </div>
        </div>
        <div class="rawLine">Raw: ${this._safeText(raw)}</div>
      `;
        }
        // SETUP view: show setup message + settings button, raw line
        else if (ui.mode === "SETUP") {
            bodyHtml = `
        <div class="notice">
          <ha-icon icon="mdi:alert-outline"></ha-icon>
          <div class="msg">${ui.msg}</div>
          <div style="margin-left:auto;">
            <button class="btn" id="settingsBtn" title="Settings">
              <ha-icon icon="mdi:cog"></ha-icon>
            </button>
          </div>
        </div>
        <div class="rawLine">Raw: ${this._safeText(raw)}</div>

        <div class="footer">
          <div class="warn">
            <ha-icon icon="mdi:information-outline"></ha-icon>
            <div>${warningText || "Configure tank size + calibrate when ready"}</div>
          </div>
        </div>
        ${simEnabled ? `<div class="simNote">Values are simulated</div>` : ``}
      `;
        }
        // OK view: show gauge + liters + cm, plus warning footer if needed
        else {
            const pctText = pct !== null ? `${pct.toFixed(1)}%` : "—%";
            const gauge = this._renderGaugeArc(pct ?? 0);

            bodyHtml = `
        <div class="layout">
          <div>
            ${gauge}
          </div>

          <div class="metrics">
            <div class="pctText">${pctText}</div>

            <div class="metricRow">
              <ha-icon icon="mdi:water-outline"></ha-icon>
              <div><b>${this._safeText(liters !== null ? liters.toFixed(2) : null)}</b> L</div>
            </div>

            <div class="metricRow">
              <ha-icon icon="mdi:ruler"></ha-icon>
              <div><b>${this._safeText(cm !== null ? cm.toFixed(1) : null)}</b> cm</div>
            </div>
          </div>
        </div>

        <div class="footer">
          <div class="warn">
            ${warningText
                    ? `<ha-icon icon="mdi:alert-outline"></ha-icon><div>${warningText}</div>`
                    : `<ha-icon icon="mdi:check-circle-outline"></ha-icon><div>All readings valid</div>`
                }
          </div>

          <button class="btn" id="settingsBtn" title="Settings">
            <ha-icon icon="mdi:cog"></ha-icon>
          </button>
        </div>
        ${simEnabled ? `<div class="simNote">Values are simulated</div>` : ``}
      `;
        }

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
      </ha-card>
    `;

        this.shadowRoot.innerHTML = html;

        const btn = this.shadowRoot.getElementById("settingsBtn");
        if (btn) btn.onclick = () => this._openSettingsModal();

        const simOff = this.shadowRoot.getElementById("simOffBtn");
        if (simOff && this._config.simulation_enabled_entity) {
            simOff.onclick = (e) => {
                e.stopPropagation();
                this._hass.callService("switch", "turn_off", {
                    entity_id: this._config.simulation_enabled_entity,
                });
            };
        }
    }
}

customElements.define(CARD_TAG, WaterTankCard);
console.info(`%c${CARD_TAG} v${VERSION} loaded`, "color: #03a9f4;");
