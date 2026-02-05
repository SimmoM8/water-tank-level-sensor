export class Device {
    constructor({
        id,
        name = "Water Tank Level Monitor",
        online = false,
        probeConnected = false,
        rawValue = null,
        calDry = null,
        calWet = null,
        lastUpdated = null,
        warnings = []
    } = {}) {
        this.id = String(id ?? "");
        this.name = name;
        this.online = online;                   // device online status
        this.probeConnected = probeConnected;   // probe connection status
        this.rawValue = rawValue;           // current raw reading from probe
        this.calDry = calDry;                   // saved dry calibration
        this.calWet = calWet;                   // saved wet calibration
        this.lastUpdated = lastUpdated;         // timestamp of last update
        this.warnings = warnings;               // array of warning messages
    }
}
