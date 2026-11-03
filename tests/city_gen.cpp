// Command line city generator (engine/CityGen.h), the same as the editor's Tools > City generator > Generate.
//   city_gen <settings.city> <out.level> [--base level-with-sky-and-sun.level] [--plan-only]
// Without --base the level gets the default environment. With it, the base level's environment,
// loading title and hint, player start and top-level entities (gameplay: quests, creatures, pickups) stay (pass the map itself to regenerate it). Prints the plan's statistics and warnings.
#include <engine/CityGen.h>
#include <engine/Scene.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: city_gen <settings.city> <out.level> [--base level] [--plan-only]\n");
        return 2;
    }
    const char* base = nullptr;
    bool planOnly = false;
    const char* svg = nullptr;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--base") && i + 1 < argc)
            base = argv[++i];
        else if (!std::strcmp(argv[i], "--svg") && i + 1 < argc)
            svg = argv[++i];
        else if (!std::strcmp(argv[i], "--plan-only"))
            planOnly = true;
    }
    using namespace engine::city;
    Settings settings;
    Kit kit;
    std::string err;
    if (!loadSettings(settings, argv[1], &err) || !loadKit(kit, settings.kit, &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    const auto t0 = std::chrono::steady_clock::now();
    const Plan plan = generate(settings, kit);
    const auto t1 = std::chrono::steady_clock::now();
    const auto& s = plan.stats;
    std::printf("plan: %zu objects in %.0f ms: %d buildings, %d gateways, %.0f m street wall (worst fill %.3f m), %d lamps, %d trees, "
                "%d wrecks, %d plants, %d boxes\n",
                plan.placements.size(), std::chrono::duration<double, std::milli>(t1 - t0).count(), s.buildings, s.gateways, s.frontage,
                s.worstFill, s.lamps, s.trees, s.wrecks, s.plants, s.boxes);
    if (settings.layout == Layout::Organic)
        std::printf("organic: %d roads, %d blocks, %d parcels, %d grammar houses (%zu ground shapes)\n", s.roads, s.blocks, s.parcels, s.generated,
                    plan.ground.shapes.size());
    for (const auto& w : plan.warnings)
        std::printf("warning: %s\n", w.c_str());
    if (svg) { // the layout preview from above (north up), as the editor draws it over the viewport
        if (FILE* f = std::fopen(svg, "w")) {
            const glm::vec3 lo = plan.boundsMin - glm::vec3(10.0f), hi = plan.boundsMax + glm::vec3(10.0f);
            std::fprintf(f, "<svg xmlns='http://www.w3.org/2000/svg' viewBox='%g %g %g %g' width='1400' height='%d'>\n", lo.x, lo.z,
                         hi.x - lo.x, hi.z - lo.z, static_cast<int>(1400.0f * (hi.z - lo.z) / (hi.x - lo.x)));
            std::fprintf(f, "<rect x='%g' y='%g' width='%g' height='%g' fill='#202326'/>\n", lo.x, lo.z, hi.x - lo.x, hi.z - lo.z);
            for (std::size_t i = 0; i + 1 < plan.lines.size(); i += 2) {
                const glm::vec4 c = plan.lineColors[i];
                std::fprintf(f, "<line x1='%.2f' y1='%.2f' x2='%.2f' y2='%.2f' stroke='rgb(%d,%d,%d)' stroke-width='0.5'/>\n", plan.lines[i].x,
                             plan.lines[i].z, plan.lines[i + 1].x, plan.lines[i + 1].z, static_cast<int>(c.r * 255), static_cast<int>(c.g * 255),
                             static_cast<int>(c.b * 255));
            }
            for (const auto& p : plan.placements)
                if (p.kind == Placement::Kind::Model) {
                    const std::string n = p.path.filename().string();
                    const char* col = n.find("lamp") != std::string::npos ? "#ffd040" : n.find("car_") != std::string::npos ? "#ff5040"
                                    : p.sway > 0.2f && p.sway < 0.5f ? "#30c040" : nullptr;
                    if (col)
                        std::fprintf(f, "<circle cx='%.2f' cy='%.2f' r='%g' fill='%s'/>\n", p.position.x, p.position.z,
                                     p.sway > 0.2f && p.sway < 0.5f ? 2.5 : 0.8, col);
                }
            std::fprintf(f, "</svg>\n");
            std::fclose(f);
        }
    }
    if (planOnly)
        return 0;
    engine::Scene scene;
    if (base && !engine::loadScene(scene, base, &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    const bool baseStart = base && scene.playerStart != glm::vec3(0.0f);
    if (base) { // keep the environment, the player start and the hand-placed entities of the base level
        scene.boxes.clear();
        scene.models.clear();
        std::erase_if(scene.entities, [](const engine::Entity& e) { return !e.parent.empty(); }); // the city's own markers go with it
        scene.groups.clear();
        scene.prefabs.clear();
        scene.terrain.reset();
        scene.probes.reset();
    }
    const auto t2 = std::chrono::steady_clock::now();
    CityObjects objects = buildObjects(plan);
    const auto t3 = std::chrono::steady_clock::now();
    std::printf("objects in %.1f s: %zu models, %zu boxes, %d houses written", std::chrono::duration<double>(t3 - t2).count(), objects.models.size(),
                objects.boxes.size(), objects.housesWritten);
    if (objects.terrain)
        std::printf(", terrain %d samples, %zu foliage instances", objects.terrain->resolution, objects.terrain->foliage.size());
    std::printf("\n");
    for (const auto& e : objects.errors)
        std::printf("error: %s\n", e.c_str());
    replaceCity(scene, settings.group, std::move(objects));
    if (!baseStart)
        scene.playerStart = settings.origin;
    if (!engine::saveScene(scene, argv[2], &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    std::printf("wrote %s\n", argv[2]);
    return 0;
}
