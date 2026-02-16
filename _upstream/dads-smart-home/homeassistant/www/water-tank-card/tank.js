export class Tank {
    constructor({
        id,
        name = "Water Tank",
        capacityLiters = null,
        tankHeightCm = null,
        probe = null,
    } = {}) {
        this.id = String(id ?? "");
        this.name = name;

        // Physical/known properties (source of truth)
        this.capacityLiters = capacityLiters; // total tank capacity in liters
        this.tankHeightCm = tankHeightCm;     // usable height in cm (rod length / tank height reference)

        // Associated domain model
        this.probe = probe; // associated Probe instance
    }

    get percent() {
        return this.probe ? this.probe.percent : null;
    }

    get liters() {
        const p = this.percent;
        if (!Number.isFinite(this.capacityLiters) || !Number.isFinite(p)) return null;
        return (p / 100) * this.capacityLiters;
    }

    get waterHeightCm() {
        const p = this.percent;
        if (!Number.isFinite(this.tankHeightCm) || !Number.isFinite(p)) return null;
        return (p / 100) * this.tankHeightCm;
    }
}