export class Probe {
    constructor({
        id,
        name = "Level Sensor",
        rawValue = null,
        calDry = null,
        calWet = null,
    } = {}) {
        this.id = String(id ?? "");
        this.name = name;
        this.rawValue = rawValue; // raw ADC / RC value of capacitive probe
        this.calDry = calDry; // saved dry calibration
        this.calWet = calWet; // saved wet calibration
    }

    /* ---------- calibration ---------- */

    get hasCalibration() {
        return (
            // checks for valid calibration values
            Number.isFinite(this.calDry) &&
            Number.isFinite(this.calWet) &&
            this.calWet > this.calDry);
    }

    updateCalibration(dry, wet) {
        if (Number.isFinite(dry)) this.calDry = dry;
        if (Number.isFinite(wet)) this.calWet = wet;
    }

    clearCalibration() {
        this.calDry = null;
        this.calWet = null;
    }

    /* ---------- raw value ---------- */

    updateRawValue(rawValue) {
        if (Number.isFinite(rawValue)) {
            this.rawValue = rawValue;
        }
    }

    /* ---------- helpers ---------- */

    get percent() {
        if (!this.hasCalibration || !Number.isFinite(this.rawValue)) {
            return null;
        }

        // applied level percent based on current raw value and applied calibration
        const percent = ((this.rawValue - this.calDry) / (this.calWet - this.calDry)) * 100;

        // clamp to 0-100%
        return Math.min(Math.max(percent, 0), 100);
    }
}