/* water-tank-card.js
 * Minimal custom Home Assistant Lovelace card (no build step).
 * v0.1: renders state machine (OK / SETUP / ERROR / OFFLINE) using entity states only.
 */

const CARD_TAG = "water-tank-card";
const VERSION = "0.1.0";

class WaterTankCard extends HTMLElement {
    constructor() {
        super();
        this.attachShadow({ mode: "open" });
        this._config = null;
        this._hass = null;
    }

    setConfig(config) {
        // Required fields (keep this strict so it fails loudly)
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
            if (!config[key]) {
                throw new Error(`Missing required config key: ${key}`);
            }
        }

        this._config = {
            title: "Water Tank",
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

    _safeText(v, fallback = "—") {
        if (v === null || v === undefined) return fallback;
        if (typeof v === "number" && !Number.isFinite(v)) return fallback;
        const s = String(v);
        return s.length ? s : fallback;
    }

    _computeUiState() {
        // Online/offline is based on your status topic entity
        const status = this._state(this._config.status_entity); // e.g. "online" or "offline"
        const offline = !status || status === "offline";

        const probeConnected = this._isOn(this._config.probe_entity);
        const percentValid = this._isOn(this._config.percent_valid_entity);
        const calState = this._state(this._config.calibration_entity); // needs_calibration | calibrating | calibrated

        // Priority rules:
        // 1) Offline overrides everything
        if (offline) {
            return { mode: "OFFLINE", msg: "Device offline", status };
        }

        // 2) Probe disconnected (or unreliable) → error-only view
        if (!probeConnected) {
            return { mode: "ERROR", msg: "Probe disconnected", status };
        }

        // 3) Not valid percent → setup/calibration view
        if (!percentValid || calState === "needs_calibration") {
            const m = calState === "calibrating" ? "Calibrating…" : "Needs calibration";
            return { mode: "SETUP", msg: m, status };
        }

        // 4) OK
        return { mode: "OK", msg: "OK", status };
    }

    _render() {
        if (!this._config || !this._hass) return;

        const ui = this._computeUiState();

        const pct = this._num(this._config.percent_entity);
        const liters = this._num(this._config.liters_entity);
        const cm = this._num(this._config.cm_entity);
        const raw = this._state(this._config.raw_entity);

        const onlineText = ui.mode === "OFFLINE" ? "Offline" : "Online";

        // Minimal styles (we’ll refine later)
        const css = `
      :host { display:block; }
      ha-card {
        border-radius: 18px;
        padding: 16px;
      }
      .header {
        display:flex;
        align-items:flex-start;
        justify-content:space-between;
        gap:12px;
        margin-bottom: 10px;
      }
      .title {
        font-size: 16px;
        font-weight: 700;
        line-height: 1.2;
      }
      .corner {
        font-size: 12px;
        opacity: 0.8;
      }
      .body { display:flex; flex-direction:column; gap: 12px; }

      /* OK state */
      .pct {
        font-size: 40px;
        font-weight: 800;
        line-height: 1;
      }
      .row { display:flex; gap: 18px; align-items:center; }
      .metric { display:flex; gap: 8px; align-items:center; font-size: 14px; }
      .metric b { font-size: 16px; }

      /* Simple progress bar (placeholder for gauge, v0.1) */
      .bar {
        height: 10px;
        border-radius: 999px;
        overflow:hidden;
        background: rgba(0,0,0,0.10);
      }
      .bar > div {
        height: 10px;
        width: 0%;
        background: var(--primary-color);
      }

      /* Error/setup box */
      .notice {
        display:flex;
        gap: 10px;
        align-items:center;
        padding: 12px;
        border-radius: 14px;
        background: rgba(255, 170, 0, 0.12);
      }
      .notice.error {
        background: rgba(255, 0, 0, 0.10);
      }
      .notice .msg {
        font-weight: 700;
      }
      .footer {
        display:flex;
        justify-content:space-between;
        align-items:center;
        gap:12px;
        opacity:0.9;
        font-size: 12px;
      }
      .btn {
        cursor:pointer;
        border:none;
        border-radius: 999px;
        padding: 8px 10px;
        background: rgba(0,0,0,0.06);
      }
    `;

        // Build OK content vs setup/error content
        let mainHtml = "";

        if (ui.mode === "OK") {
            const pctText = pct !== null ? `${pct.toFixed(1)}%` : "—%";
            const pctWidth = pct !== null ? Math.max(0, Math.min(100, pct)) : 0;

            mainHtml = `
        <div class="pct">${pctText}</div>
        <div class="bar"><div style="width:${pctWidth}%;"></div></div>

        <div class="row">
          <div class="metric">
            <ha-icon icon="mdi:water-outline"></ha-icon>
            <div><b>${this._safeText(liters !== null ? liters.toFixed(2) : null)}</b> L</div>
          </div>
          <div class="metric">
            <ha-icon icon="mdi:ruler"></ha-icon>
            <div><b>${this._safeText(cm !== null ? cm.toFixed(1) : null)}</b> cm</div>
          </div>
        </div>

        <div class="footer">
          <div>All readings valid</div>
          <button class="btn" id="settingsBtn" title="Settings">
            <ha-icon icon="mdi:cog"></ha-icon>
          </button>
        </div>
      `;
        } else {
            const isError = ui.mode === "ERROR" || ui.mode === "OFFLINE";
            mainHtml = `
        <div class="notice ${isError ? "error" : ""}">
          <ha-icon icon="${isError ? "mdi:alert-circle-outline" : "mdi:alert-outline"}"></ha-icon>
          <div class="msg">${ui.msg}</div>
        </div>

        <div class="footer">
          <div>Raw: ${this._safeText(raw)}</div>
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
        <div class="body">
          ${mainHtml}
        </div>
      </ha-card>
    `;

        this.shadowRoot.innerHTML = html;

        // Settings button behavior (modal comes next step; for now open More Info)
        const btn = this.shadowRoot.getElementById("settingsBtn");
        if (btn) {
            btn.onclick = () => {
                // Open HA More Info for raw sensor as a placeholder "settings"
                const event = new CustomEvent("hass-more-info", {
                    bubbles: true,
                    composed: true,
                    detail: { entityId: this._config.raw_entity },
                });
                this.dispatchEvent(event);
            };
        }
    }
}

customElements.define(CARD_TAG, WaterTankCard);
console.info(`%c${CARD_TAG} v${VERSION} loaded`, "color: #03a9f4;");