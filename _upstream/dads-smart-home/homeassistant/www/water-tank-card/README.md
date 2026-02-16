## Water Tank Card

### Install the resource
1) Place the files in `www/water-tank-card/` (HACS: add as a custom repository and select "Lovelace" resource).
2) Add a Lovelace resource:
	- Settings → Dashboards → Resources → Add resource
	- URL: `/local/water-tank-card/water-tank-card.js`
	- Type: `JavaScript Module`

### Add the dashboard from the blueprint
1) Copy `config/blueprints/dashboard/water_tank_dashboard.yaml` into your Home Assistant `config/blueprints/dashboard/` folder (or import the file via the UI).
2) In Settings → Dashboards, create a dashboard from blueprint and pick **Water Tank Dashboard**.
3) When prompted, select your water tank device. The card will auto-resolve entities via the device registry.

### Manual card usage
Add a card:

```yaml
type: custom:water-tank-card
device_id: <your_device_id>
title: Water Tank
```

If you prefer explicit entities, you can still provide `percent_entity`, `liters_entity`, `cm_entity`, `status_entity`, `probe_entity`, `percent_valid_entity`, `calibration_entity`, and `raw_entity` plus any optional ones.
