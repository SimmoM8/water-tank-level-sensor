/* water-tank-card.js
 * Custom Home Assistant Lovelace card (no build step).
 * v0.3: arc gauge + error-only view + warning footer + Browser Mod settings modal.
 */

const CARD_TAG = "water-tank-card";
const VERSION = "0.3.0";

class WaterTankCard extends HTMLElement {
    constructor() {
        super();
        this.attachShadow({ mode: "open" });
        this._config = null;
        this._hass = null;
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
    _openSettingsModal() {
        const hasBrowserMod = !!this._hass?.services?.browser_mod?.popup;

        // Build a “best effort” modal. If optional entities are missing, we omit that section.
        const statusEntities = [
            this._config.status_entity,
            this._config.calibration_entity,
            this._config.probe_entity,
            this._config.percent_valid_entity,
            this._config.raw_entity,
        ];

        const configEntities = [];
        if (this._config.tank_volume_entity) configEntities.push(this._config.tank_volume_entity);
        if (this._config.rod_length_entity) configEntities.push(this._config.rod_length_entity);

        const calEntities = [];
        if (this._config.calibrate_dry_entity) calEntities.push(this._config.calibrate_dry_entity);
        if (this._config.calibrate_wet_entity) calEntities.push(this._config.calibrate_wet_entity);
        if (this._config.clear_calibration_entity) calEntities.push(this._config.clear_calibration_entity);

        if (hasBrowserMod) {
            const cards = [
                {
                    type: "entities",
                    title: "Status",
                    entities: statusEntities,
                },
            ];

            if (configEntities.length) {
                cards.push({
                    type: "entities",
                    title: "Tank Setup",
                    entities: configEntities,
                });
            }

            if (calEntities.length) {
                cards.push({
                    type: "entities",
                    title: "Calibration",
                    entities: calEntities,
                });
            }

            this._hass.callService("browser_mod", "popup", {
                title: this._config.title || "Water Tank Settings",
                content: {
                    type: "vertical-stack",
                    cards,
                },
            });
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
      `;
        }

        const html = `
      <style>${css}</style>
      <ha-card>
        <div class="header">
          <div class="title">${this._config.title}</div>
          <div class="corner">${onlineText}</div>
        </div>
        ${bodyHtml}
      </ha-card>
    `;

        this.shadowRoot.innerHTML = html;

        const btn = this.shadowRoot.getElementById("settingsBtn");
        if (btn) btn.onclick = () => this._openSettingsModal();
    }
}

customElements.define(CARD_TAG, WaterTankCard);
console.info(`%c${CARD_TAG} v${VERSION} loaded`, "color: #03a9f4;");