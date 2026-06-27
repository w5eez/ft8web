# Map data attribution

- `cities15000.zip` — © [GeoNames](https://www.geonames.org/), licensed
  [CC-BY 4.0](https://creativecommons.org/licenses/by/4.0/). Used to generate
  the city label layer (`frontend/ft8web-ui/public/cities.json`).
- `ne_50m_admin_1_states_provinces_lines.geojson`,
  `ne_110m_admin_1_states_provinces_lines.geojson` — made with
  [Natural Earth](https://www.naturalearthdata.com/), public domain.
- `us-states.json` and `frontend/ft8web-ui/public/world.geo.json` — GeoJSON
  distributed via [PublicaMundi/MappingAPI](https://github.com/PublicaMundi/MappingAPI),
  derived from Natural Earth and US Census Bureau cartographic boundary data.

Regenerate the frontend map assets with `tools/gen_cities.py` and
`tools/gen_map_extras.py`.
