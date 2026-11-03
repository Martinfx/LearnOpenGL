# Dead City street props

Post-apocalyptic street dressing for the Dead City level: rusted and faded drums, tyres, dumpsters, bins, shipping containers,
brick ruins and rubble, curbs and broken sidewalk, US traffic signals, cones, US road signs, Jersey barriers, railings and
guardrail, each with intact and damaged / bent / fallen variants.

- 1 unit = 1 m, origin at the bottom centre, +Y up. Long side along +X; sign faces and signal lenses face -Z in Blender = **+Z in the engine**.
- Low poly (16 to 6000 tris); `deadcity.json` lists size (x, y up, z), tris, physics kind (static / dynamic), mass and collider (box, cylinder or mesh).
- Big models also have a decimated `<name>_lod1.glb` (switch at ~40 m).
- Textures: 1K PBR (albedo, roughness, normal) embedded in each GLB.
- Road decals that belong with these (cracks, potholes) are in `assets/textures/decals/`; cracked asphalt and sidewalk materials in `assets/materials/pbr/`.

| Group | Models |
|---|---|
| Drums (200 l / 55 gal) | `drum_blue`, `drum_blue_dented`, `drum_cluster`, `drum_green`, `drum_red`, `drum_red_lying`, `drum_rust`, `drum_rust_open`, `drum_yellow` |
| Tyres | `tyre_leaning`, `tyre_pile`, `tyre_stack` |
| Dumpsters | `dumpster_blue_open`, `dumpster_green`, `dumpster_green_tipped` |
| Wheelie bins | `wheelie_bin_green`, `wheelie_bin_grey_open`, `wheelie_bin_tipped` |
| Shipping containers (20 ft ISO) | `container_blue`, `container_red`, `container_rust_open` |
| Brick ruins | `brick_pile_large`, `brick_wall_ruin_a`, `brick_wall_ruin_corner`, `brick_wall_ruin_low`, `brick_wall_ruin_window` |
| Concrete and rubble | `concrete_chunks`, `concrete_rubble_pile`, `concrete_slab_rebar`, `concrete_slab_rebar_flat`, `rubble_mound_large` |
| Curbs and sidewalk | `curb_1m`, `curb_2m`, `curb_broken`, `curb_corner`, `sidewalk_slab_fragments`, `sidewalk_slabs_broken`, `sidewalk_slabs_heaved` |
| US traffic signals | `traffic_signal_fallen`, `traffic_signal_head_loose`, `traffic_signal_mast`, `traffic_signal_mast_sagging`, `traffic_signal_post_leaning` |
| Cones and channelizer drums | `traffic_cone`, `traffic_cone_crushed`, `traffic_cone_fallen`, `traffic_drum`, `traffic_drum_fallen` |
| US road signs | `sign_curve`, `sign_donotenter`, `sign_noparking`, `sign_oneway`, `sign_oneway_leaning`, `sign_pedxing`, `sign_roadwork`, `sign_roadwork_fallen`, `sign_speed35`, `sign_speed35_leaning`, `sign_stop`, `sign_stop_bent`, `sign_stop_panel_loose`, `sign_street_name`, `sign_yield`, `sign_yield_bent` |
| Jersey barriers | `jersey_barrier`, `jersey_barrier_broken`, `jersey_barrier_row`, `jersey_barrier_short`, `jersey_barrier_tipped` |
| Railings and guardrail | `guardrail_wbeam`, `guardrail_wbeam_crashed`, `guardrail_wbeam_end`, `railing_bars_2m`, `railing_bars_bent`, `railing_pipe_2m`, `railing_pipe_bent`, `railing_pipe_broken` |

## Rebuild

```
python3 -I source/textures.py /tmp/dc/tex      # texture set (and the road decals)
python3 -I source/dc_props.py /tmp/dc/tex [drums tyres bins containers ruins street signals cones signs barriers railings]
```
Needs `pip install bpy` (Blender as a Python module), numpy and Pillow. Licence and sources: CREDITS.md.
