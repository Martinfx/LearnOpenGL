#pragma once
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace engine::city {

// ---------------------------------------------------------------------------
// Street networks grown from a tensor field (Chen, Esch, Wonka, Mueller, Zhang:
// "Interactive Procedural Street Modeling", SIGGRAPH 2008 - the method behind
// CityEngine's street growth), the blocks between the streets, and the polygon
// tools the generator needs to cut blocks into lots and parcels.
//
//   field   a smooth direction field: grid bases (one per district, each with its
//           own heading) blended with a radial basis around the centre (ring roads)
//           and slow noise (streets that bend with the land);
//   streets streamlines of the field: main roads along the major direction at the
//           main spacing, side streets along the minor one at the side spacing,
//           evenly spaced (Jobard and Lefer seeding); ends that stop short are
//           carried on to the next street, dead ends are pruned;
//   blocks  the faces of the planar street graph (one polygon per block, each edge
//           knowing the class of the street it faces).
// Everything here is pure and deterministic (own noise, no std distributions).
// ---------------------------------------------------------------------------

using Poly = std::vector<glm::vec2>; // counter-clockwise (x right, y up) unless said otherwise

struct NetworkSettings {
    float size = 640.0f;          // side of the square town area (m), centred on 0
    float angle = 0.0f;           // heading of the main district's grid (degrees)
    float districtTurn = 30.0f;   // the other districts turn up to this much from it (degrees)
    int districts = 3;            // grid bases (1 = one straight grid)
    float radial = 0.35f;         // weight of the radial basis (ring roads around the centre), 0..1
    float curvature = 0.25f;      // slow noise turning the field (radians at 1)
    float noiseScale = 260.0f;    // m per noise wave
    float majorSpacing = 150.0f;  // between main roads
    float minorSpacing = 75.0f;   // between side streets
    float alleySpacing = 0.0f;    // between back lanes through the blocks (0 = none)
    float step = 3.0f;            // tracing step (m)
    float join = 0.8f;            // short ends carried on up to join * spacing to the next street
    int majorClass = 3;           // StreetClass of major streamlines (main)
    int minorClass = 2;           // ... minor ones (side)
    int alleyClass = 1;           // ... lanes (alley)
    int boundaryClass = 2;        // the road around the town (0 = none: an open edge)
    std::uint32_t seed = 1;
};

struct Road {
    std::vector<glm::vec2> points;
    int cls = 2;
};

struct Block {
    Poly outline;                 // street axes around the block, counter-clockwise
    std::vector<int> edgeClass;   // class of the street on edge i (outline[i] -> outline[i + 1])
};

struct RoadNetwork {
    std::vector<Road> roads;      // graph edges (junction to junction), simplified
    std::vector<Block> blocks;
    std::vector<glm::vec2> junctions;
};

// The major direction of the field at p (unit, sign arbitrary).
glm::vec2 fieldDirection(const NetworkSettings& s, glm::vec2 p);
RoadNetwork buildRoadNetwork(const NetworkSettings& s);

// ------------------------------------------------------------------ polygons
float signedArea(const Poly& p);
glm::vec2 centroid(const Poly& p);
bool pointInPolygon(const Poly& p, glm::vec2 q);
bool isSimple(const Poly& p);                     // no two non-adjacent edges cross
float distanceToPolygonEdge(const Poly& p, glm::vec2 q);
// Douglas-Peucker on a closed polygon; `keep` (optional, per vertex) marks vertices that stay.
Poly simplifyPolygon(const Poly& p, float tolerance, const std::vector<bool>* keep = nullptr,
                     std::vector<int>* kept = nullptr);
// Inward offset by a distance per edge (mitred corners). Edges that shrink to nothing
// are dropped and their neighbours meet; false when nothing valid is left.
bool insetPolygon(const Poly& p, const std::vector<float>& distance, Poly& out, std::vector<int>* sourceEdge = nullptr);
// Mitred inset keeping every vertex (vertex i of the result belongs to vertex i):
// for frontage strips between the two. False if any strip turns inside out.
bool mitreInset(const Poly& p, const std::vector<float>& distance, Poly& out);
// Ear clipping; triangle indices into p (counter-clockwise).
std::vector<std::uint32_t> triangulate(const Poly& p);
// Oriented bounding box of least area: centre, unit axis of the long side, half sizes.
void minAreaRect(const Poly& p, glm::vec2& centre, glm::vec2& axis, glm::vec2& half);
// Cuts a convex or concave polygon by the line through `a` with direction `d`:
// left part (cross(d, q - a) > 0) and right part.
void splitPolygon(const Poly& p, glm::vec2 a, glm::vec2 d, Poly& left, Poly& right);

} // namespace engine::city
