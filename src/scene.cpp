#include "stillwater/scene.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace stillwater {
namespace {
constexpr double tau = 2.0 * std::numbers::pi;
Vec3 subtract(Vec3 a, Vec3 b) {
    const Vec3 result{a.x - b.x, a.y - b.y, a.z - b.z};
    return result;
}
double dot(Vec3 a, Vec3 b) {
    const double result = a.x * b.x + a.y * b.y + a.z * b.z;
    return result;
}
Vec3 cross(Vec3 a, Vec3 b) {
    const Vec3 result{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    return result;
}
Vec3 normalized(Vec3 value) {
    const double length = std::sqrt(dot(value, value));
    const Vec3 result{value.x / length, value.y / length, value.z / length};
    return result;
}
class Random final {
  public:
    double unit() {
        state_ = state_ * 1664525U + 1013904223U;
        return static_cast<double>(state_) / 4294967296.0;
    }
    double range(double low, double high) {
        const double result = low + (high - low) * unit();
        return result;
    }

  private:
    std::uint32_t state_{270923U};
};
Matrix transform(Vec3 position, Vec3 scale, double yaw) {
    const double c = std::cos(yaw), s = std::sin(yaw);
    Matrix result{{static_cast<float>(c * scale.x), 0, static_cast<float>(-s * scale.x), 0, 0,
                   static_cast<float>(scale.y), 0, 0, static_cast<float>(s * scale.z), 0,
                   static_cast<float>(c * scale.z), 0, static_cast<float>(position.x),
                   static_cast<float>(position.y), static_cast<float>(position.z), 1}};
    return result;
}
} // namespace
Matrix multiply(const Matrix& left, const Matrix& right) {
    Matrix result{};
    for (std::size_t column = 0; column < 4; ++column)
        for (std::size_t row = 0; row < 4; ++row) {
            double sum = 0;
            for (std::size_t inner = 0; inner < 4; ++inner)
                sum += left.values[inner * 4 + row] * right.values[column * 4 + inner];
            result.values[column * 4 + row] = static_cast<float>(sum);
        }
    return result;
}
Matrix perspective(double aspect) {
    const double f = 1.0 / std::tan(25.8 * std::numbers::pi / 360.0);
    Matrix result{};
    result.values[0] = static_cast<float>(f / aspect);
    result.values[5] = static_cast<float>(f);
    result.values[10] = -100.0F / 99.9F;
    result.values[11] = -1;
    result.values[14] = -10.0F / 99.9F;
    return result;
}
Matrix view_matrix() {
    const Vec3 eye{0, 4.65, 20.5}, target{0, 4.15, 0};
    const Vec3 forward = normalized(subtract(target, eye));
    const Vec3 side = normalized(cross(forward, {0, 1, 0})), up = cross(side, forward);
    Matrix result{{static_cast<float>(side.x), static_cast<float>(up.x),
                   static_cast<float>(-forward.x), 0, static_cast<float>(side.y),
                   static_cast<float>(up.y), static_cast<float>(-forward.y), 0,
                   static_cast<float>(side.z), static_cast<float>(up.z),
                   static_cast<float>(-forward.z), 0, static_cast<float>(-dot(side, eye)),
                   static_cast<float>(-dot(up, eye)), static_cast<float>(dot(forward, eye)), 1}};
    return result;
}
Vec3 project(Vec3 point, double aspect) {
    const Matrix matrix = multiply(perspective(aspect), view_matrix());
    const std::array<double, 4> input{point.x, point.y, point.z, 1};
    std::array<double, 4> clip{};
    for (std::size_t row = 0; row < 4; ++row)
        for (std::size_t column = 0; column < 4; ++column)
            clip[row] += matrix.values[column * 4 + row] * input[column];
    if (clip[3] <= 0)
        return {-10, -10, -1};
    const Vec3 result{clip[0] / clip[3] * 0.5 + 0.5, clip[1] / clip[3] * 0.5 + 0.5,
                      clip[2] / clip[3]};
    return result;
}
// Translated from Riverscape math.js (Chase Lean, MIT).
double ground_height(double x, double z) {
    const double center = 0.3 - 0.25 * z;
    const double half_width = std::max(0.45, 1.45 + 0.25 * z);
    const double channel = std::exp(-std::pow((x - center) / half_width, 2));
    const double result = 0.12 + 0.055 * std::sin(x * 1.8 + z) +
                          0.045 * std::sin(z * 2.3 - x * 0.7) + 0.34 * std::max(0.0, -z / 5) +
                          0.14 * std::exp(-((x + 5) * (x + 5) / 5 + (z + 1) * (z + 1) / 4)) -
                          0.2 * channel;
    return result;
}
Vec3 actor_position(const Actor& actor, double time) {
    const double since = time - actor.startled_from.w;
    if (actor.escape.w > 0 && since < actor.escape.w) {
        const double f = std::clamp(since / actor.escape.w, 0.0, 1.0),
                     ease = 1.0 - std::pow(1.0 - f, 3);
        const Vec3 result{actor.startled_from.x + (actor.escape.x - actor.startled_from.x) * ease,
                          actor.startled_from.y + (actor.escape.y - actor.startled_from.y) * ease,
                          actor.startled_from.z + (actor.escape.z - actor.startled_from.z) * ease};
        return result;
    }
    const double elapsed = actor.escape.w > 0 ? since - actor.escape.w : time;
    const double angle = elapsed * actor.cruise.z + actor.cruise.w;
    const Vec3 result{actor.center.x + actor.cruise.x * std::sin(angle),
                      actor.center.y + actor.cruise.y * std::sin(angle * 2),
                      actor.center.z + actor.cruise.x * 0.25 * (std::cos(angle) - 1)};
    return result;
}
// Transform a surface normal, including nonuniform parent scale.
Vec3 leaf_world_normal(Vec3 normal, const Matrix& matrix) {
    const std::array<float, 16>& m = matrix.values;
    const double sx = std::max(1e-10, static_cast<double>(m[0]*m[0]+m[1]*m[1]+m[2]*m[2]));
    const double sy = std::max(1e-10, static_cast<double>(m[4]*m[4]+m[5]*m[5]+m[6]*m[6]));
    const double sz = std::max(1e-10, static_cast<double>(m[8]*m[8]+m[9]*m[9]+m[10]*m[10]));
    return normalized({m[0]*normal.x/sx + m[4]*normal.y/sy + m[8]*normal.z/sz,
                       m[1]*normal.x/sx + m[5]*normal.y/sy + m[9]*normal.z/sz,
                       m[2]*normal.x/sx + m[6]*normal.y/sy + m[10]*normal.z/sz});
}
bool leaf_bubble_supported(const Vertex& leaf, const Instance& parent) {
    const Vec3 normal = leaf_world_normal({leaf.normal.x, leaf.normal.y, leaf.normal.z}, parent.transform);
    // Within 20 degrees of horizontal at rest, with room for gentle leaf sway.
    return std::abs(normal.y) >= 0.94;
}
Matrix leaf_bubble_transform(const Instance& bubble, const Vertex& leaf,
                             const Instance& parent, double time) {
    const double age = std::fmod(time + bubble.behavior.y, bubble.anatomy.w);
    const double hold = bubble.anatomy.w - 6, released = std::max(0.0, age - hold);
    const double growth = std::clamp(age / hold, 0.0, 1.0);
    double radius = bubble.anatomy.z * (0.70 + 0.30 * growth * growth * (3 - 2 * growth));
    const double t = time - released, x = leaf.anchor.x, z = leaf.anchor.z;
    const double strength = 0.34 * std::sin(t * 0.031) + 0.15 * std::sin(t * 0.055 - x * 0.34 - z * 0.19) +
        0.03 * std::sin(t * 0.235 + x * 1.7 + z * 1.1) + 0.03 * std::sin(t * 0.155 + x * 0.6 - z * 2.3);
    // Match the single-precision shader seed before using double for the geometry query.
    const float seed_value = std::sin(leaf.anchor.x * 12.9898F + leaf.anchor.z * 78.233F) * 43758.5453F;
    const double seed = seed_value - std::floor(seed_value);
    const double phase = seed * 6.2832 + x * 0.9;
    const double distance = std::max(0.0001, static_cast<double>(leaf.along.w));
    const double drag = leaf.bend.w * (leaf.bend.x + 0.22 * leaf.bend.z) / std::sqrt(1.0484) * strength;
    const double saturation = 1 + 0.06 * leaf.along.w * leaf.along.w;
    const double gain = leaf.bend.w * (0.012 + 0.02 * strength);
    const double power = std::pow(distance, 0.3);
    const double theta = t * 0.95 - 1.05 * leaf.along.w + phase;
    const double ripple = t * 1.55 - 1.7 * leaf.along.w + phase * 2.3;
    const double shape = std::sin(theta) + 0.3 * std::sin(ripple);
    const double motion = drag * 0.09 * leaf.along.w * leaf.along.w / saturation + gain * distance * power * shape;
    const double slope = drag * 0.18 * leaf.along.w / (saturation * saturation) +
        gain * 1.3 * power * shape - gain * distance * power * (1.05 * std::cos(theta) + 0.51 * std::cos(ripple));
    const double bend_normal = leaf.bend.x * leaf.normal.x + leaf.bend.y * leaf.normal.y + leaf.bend.z * leaf.normal.z;
    Vec3 normal = normalized({leaf.normal.x - leaf.along.x * slope * bend_normal,
                              leaf.normal.y - leaf.along.y * slope * bend_normal,
                              leaf.normal.z - leaf.along.z * slope * bend_normal});
    normal = leaf_world_normal(normal, parent.transform);
    const double support = std::clamp((std::abs(normal.y) - 0.906307787) / (0.94 - 0.906307787), 0.0, 1.0);
    radius *= support * support * (3 - 2 * support);
    if (normal.y > 0)
        normal = {-normal.x, -normal.y, -normal.z};
    const Vec3 local{leaf.position.x + leaf.bend.x * motion,
                     leaf.position.y + leaf.bend.y * motion,
                     leaf.position.z + leaf.bend.z * motion};
    const Matrix& m = parent.transform;
    Vec3 world{m.values[0]*local.x + m.values[4]*local.y + m.values[8]*local.z + m.values[12],
               m.values[1]*local.x + m.values[5]*local.y + m.values[9]*local.z + m.values[13],
               m.values[2]*local.x + m.values[6]*local.y + m.values[10]*local.z + m.values[14]};
    world.x += normal.x * radius * 0.90;
    world.y += normal.y * radius * 0.90;
    world.z += normal.z * radius * 0.90;
    world.y += released * bubble.behavior.z;
    world.x += 0.07 * released * std::sin(released * 2 + bubble.behavior.y);
    world.z += released * 0.025;
    const double fade = std::clamp((released - 5.4) / 0.6, 0.0, 1.0);
    radius = std::max(0.00001, radius * (1 - fade * fade * (3 - 2 * fade)));
    return transform(world, {radius, radius * 0.9, radius}, 0);
}
Scene::Scene() {
    vertices_.reserve(7000);
    indices_.reserve(35000);
    instances_.reserve(1800);
    objects_.reserve(1800);
    actors_.reserve(20);
    build_geometry();
    build_habitat();
    build_animals();
    compile_batches();
    statistics_.geometry_builds = 1;
    statistics_.revision = 1;
    actor_revisions_.resize(actors_.size(), 1);
    instance_revisions_.resize(instances_.size(), 1);
}
void Scene::build_geometry() {
    Mesh& sphere = meshes_[0];
    sphere.first_index = static_cast<std::uint32_t>(indices_.size());
    constexpr std::uint32_t rings = 10, segments = 16;
    for (std::uint32_t row = 0; row <= rings; ++row)
        for (std::uint32_t column = 0; column <= segments; ++column) {
            const double lat = std::numbers::pi * row / rings, lon = tau * column / segments;
            const Float4 p{static_cast<float>(std::sin(lat) * std::cos(lon)),
                           static_cast<float>(std::cos(lat)),
                           static_cast<float>(std::sin(lat) * std::sin(lon)), 1};
            vertices_.push_back({p, {p.x, p.y, p.z, 0}});
        }
    for (std::uint32_t row = 0; row < rings; ++row)
        for (std::uint32_t column = 0; column < segments; ++column) {
            const std::uint32_t a = row * (segments + 1) + column, b = a + segments + 1;
            indices_.insert(indices_.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    sphere.index_count = static_cast<std::uint32_t>(indices_.size()) - sphere.first_index;
    for (std::uint32_t kind = 1; kind <= 3; ++kind) {
        Mesh& mesh = meshes_[kind];
        mesh.first_index = static_cast<std::uint32_t>(indices_.size());
        if (kind == 2) {
            // Curved rachis and individually modeled pinnate leaflets.
            for (unsigned int tier = 0; tier < 15; ++tier) {
                const double height = 0.06 + static_cast<double>(tier) * 0.062;
                const double reach = 0.32 * std::pow(std::sin(std::numbers::pi * height), 0.75);
                for (int side = -1; side <= 1; side += 2) {
                    const std::uint32_t leaflet = static_cast<std::uint32_t>(vertices_.size());
                    constexpr unsigned int leaflet_segments = 6;
                    for (unsigned int segment = 0; segment <= leaflet_segments; ++segment) {
                        const double t = static_cast<double>(segment) / leaflet_segments;
                        const double x = side * reach * t + 0.07 * std::sin(height * 3);
                        const double y = height + 0.1 * t - 0.035 * t * t;
                        const double z =
                            0.28 * height * height + 0.10 * std::sin(t * std::numbers::pi);
                        const double half_width = 0.025 * std::sin(std::numbers::pi * t);
                        vertices_.push_back(
                            {{static_cast<float>(x), static_cast<float>(y - half_width),
                              static_cast<float>(z), 1},
                             {0, 0.2F, 1, 0}});
                        vertices_.push_back(
                            {{static_cast<float>(x), static_cast<float>(y + half_width),
                              static_cast<float>(z), 1},
                             {0, 0.2F, 1, 0}});
                        if (segment < leaflet_segments) {
                            const std::uint32_t a = leaflet + segment * 2;
                            indices_.insert(indices_.end(), {a, a + 1, a + 2, a + 1, a + 3, a + 2});
                        }
                    }
                }
            }
        }
        const std::uint32_t base = static_cast<std::uint32_t>(vertices_.size());
        constexpr std::uint32_t steps = 20;
        for (std::uint32_t step = 0; step <= steps; ++step) {
            const double t = static_cast<double>(step) / steps;
            double width = 0.30 * std::pow(std::sin(std::numbers::pi * t), 0.7) *
                           (0.85 + 0.15 * std::sin(t * 28));
            if (kind == 2)
                width = 0.008 * (1 - t);
            if (kind == 3)
                width = 0.55 * t;
            for (int side = -1; side <= 1; side += 2)
                vertices_.push_back({{static_cast<float>(side * width + 0.07 * std::sin(t * 3)),
                                      static_cast<float>(t), static_cast<float>(0.28 * t * t), 1},
                                     {0, 0, 1, 0}});
            if (step < steps) {
                const std::uint32_t a = base + step * 2;
                indices_.insert(indices_.end(), {a, a + 1, a + 2, a + 1, a + 3, a + 2});
            }
        }
        mesh.index_count = static_cast<std::uint32_t>(indices_.size()) - mesh.first_index;
    }
    Mesh& ground = meshes_[4];
    ground.first_index = static_cast<std::uint32_t>(indices_.size());
    const std::uint32_t base = static_cast<std::uint32_t>(vertices_.size());
    constexpr std::uint32_t columns = 48, rows = 60;
    for (std::uint32_t row = 0; row <= rows; ++row)
        for (std::uint32_t column = 0; column <= columns; ++column) {
            const double x = -24.0 + column, z = 8.0 - static_cast<double>(row);
            const double dx = (ground_height(x + 0.01, z) - ground_height(x - 0.01, z)) / 0.02;
            const double dz = (ground_height(x, z + 0.01) - ground_height(x, z - 0.01)) / 0.02;
            const Vec3 n = normalized({-dx, 1, -dz});
            vertices_.push_back(
                {{static_cast<float>(x), static_cast<float>(ground_height(x, z)),
                  static_cast<float>(z), 1},
                 {static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z), 0}});
        }
    for (std::uint32_t row = 0; row < rows; ++row)
        for (std::uint32_t column = 0; column < columns; ++column) {
            const std::uint32_t a = base + row * (columns + 1) + column, b = a + columns + 1;
            indices_.insert(indices_.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
    ground.index_count = static_cast<std::uint32_t>(indices_.size()) - ground.first_index;
}
void Scene::add(ObjectKind kind, MeshKind mesh, Vec3 position, Vec3 scale, double yaw, Float4 color,
                Float4 behavior, Float4 anatomy) {
    const std::uint32_t identity = static_cast<std::uint32_t>(objects_.size() + 1),
                        index = static_cast<std::uint32_t>(instances_.size());
    instances_.push_back({transform(position, scale, yaw), color, behavior, anatomy});
    const Vec3 bound = kind == ObjectKind::kelp || kind == ObjectKind::fern
                           ? Vec3{position.x, position.y + scale.y * 0.5, position.z}
                           : position;
    objects_.push_back({identity, kind, mesh, index, bound,
                        std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)}),
                        static_cast<int>(behavior.w)});
}
void Scene::build_habitat() {
    Random random{};
    add(ObjectKind::sand, MeshKind::ground, {0, 0, 0}, {1, 1, 1}, 0, {0.53F, 0.47F, 0.31F, 1},
        {0, 0, 0, -1});
    for (std::size_t index = 0; index < 32; ++index) {
        const double z = random.range(-35, 0), side = index % 2 == 0 ? -1.0 : 1.0;
        const double x = side * random.range(6, 15) + 1.3 * std::sin(z * 0.12),
                     size = random.range(1.2, 2.6);
        add(ObjectKind::rock, MeshKind::sphere, {x, ground_height(x, z) + size * 0.35, z},
            {size, size * random.range(0.55, 1.1), size * 1.2}, random.range(0, tau),
            {0.11F, 0.13F, 0.10F, 1}, {1, static_cast<float>(index + 400), 0, -1});
    }
    for (std::size_t index = 0; index < 260; ++index) {
        const double z = random.range(-42, 4), side = index % 2 == 0 ? -1.0 : 1.0;
        const double x = side * random.range(3.5, 16) + 1.3 * std::sin(z * 0.12),
                     size = random.range(0.14, 1.1), shade = random.range(0.10, 0.25);
        add(ObjectKind::rock, MeshKind::sphere, {x, ground_height(x, z) + size * 0.22, z},
            {size, random.range(0.4, 0.8) * size, size * random.range(0.7, 1.5)},
            random.range(0, tau),
            {static_cast<float>(shade * 0.82), static_cast<float>(shade),
             static_cast<float>(shade * 0.86), 1},
            {1, static_cast<float>(index), 0, -1});
    }
    for (std::size_t index = 0; index < 220; ++index) {
        const double x = random.range(-12, 12), z = random.range(-12, 5),
                     size = random.range(0.025, 0.095);
        add(ObjectKind::rock, MeshKind::sphere, {x, ground_height(x, z) + size * 0.2, z},
            {size, size * 0.55, size * 1.3}, random.range(0, tau), {0.35F, 0.33F, 0.24F, 1},
            {1, static_cast<float>(index), 0, -1});
    }
    for (std::size_t cluster = 0; cluster < 120; ++cluster) {
        const double z = random.range(-40, 3), side = cluster % 2 == 0 ? -1.0 : 1.0;
        const double x = side * random.range(4, 15.5) + 1.2 * std::sin(z * 0.12);
        const bool tall = cluster % 3 == 0;
        const double height = tall ? random.range(3, 8.5) : random.range(0.6, 3);
        for (std::size_t blade = 0; blade < 7; ++blade) {
            const double bx = x + random.range(-0.5, 0.5), bz = z + random.range(-0.5, 0.5),
                         h = height * random.range(0.5, 1.1);
            const float green = static_cast<float>(random.range(0.18, 0.42));
            add(tall ? ObjectKind::kelp : ObjectKind::fern, tall ? MeshKind::leaf : MeshKind::frond,
                {bx, ground_height(bx, bz), bz}, {random.range(1.2, 2.4), h, 1},
                random.range(0, tau),
                {green * static_cast<float>(random.range(0.35, 0.75)), green, green * 0.23F, 1},
                {2, static_cast<float>(random.range(0, tau)),
                 static_cast<float>(random.range(0.1, 0.4)), -1});
        }
    }
    for (std::size_t index = 0; index < 84; ++index) {
        const double x = 3.5 + random.range(-0.22, 0.22), z = -1.2 + random.range(-0.35, 0.35),
                     size = random.range(0.022, 0.065);
        add(ObjectKind::bubble, MeshKind::sphere, {x, 0.2, z}, {size, size * 0.86, size}, 0,
            {0.45F, 0.73F, 0.68F, 1},
            {3, static_cast<float>(random.range(0, 10)), static_cast<float>(random.range(0.85, 1.9)),
             -1});
    }
}
void Scene::build_animals() {
    Random random{};
    for (std::size_t index = 0; index < 16; ++index) {
        const float z = static_cast<float>(index < 6 ? random.range(-22, -10)
                                                     : random.range(-9, 2)),
                    size = static_cast<float>(random.range(0.6, 1.15));
        actors_.push_back(
            {{static_cast<float>(random.range(-3.5, 3.5)), static_cast<float>(random.range(1.4, 6)),
              z, size},
             {static_cast<float>(random.range(2, 5)), static_cast<float>(random.range(0.1, 0.45)),
              static_cast<float>(random.range(0.08, 0.17)),
              static_cast<float>(random.range(0, tau))},
             {},
             {},
             {}});
        const float actor = static_cast<float>(actors_.size() - 1);
        const bool angel = index % 4 == 0, neon = index % 4 == 1;
        const Float4 color =
            neon ? Float4{0.13F, 0.52F, 0.62F, 1}
                 : (angel ? Float4{0.64F, 0.65F, 0.52F, 1} : Float4{0.75F, 0.34F, 0.10F, 1});
        const double h = angel ? 0.43 : 0.21;
        add(ObjectKind::fish, MeshKind::sphere, {0, 0, 0}, {0.65, h, 0.16}, 0, color,
            {4, static_cast<float>(index % 4), 0, actor});
        add(ObjectKind::fish, MeshKind::fin, {-0.55, 0, 0}, {0.7, 0.48, 0.7}, 0, color,
            {5, 0, 0, actor}, {1, 0, 0, 0});
        add(ObjectKind::fish, MeshKind::fin, {-0.13, h * 0.65, 0}, {0.7, angel ? 0.48 : 0.25, 0.7},
            0, color, {5, 1, 0, actor}, {2, 0, 0, 0});
        add(ObjectKind::fish, MeshKind::fin, {-0.13, -h * 0.6, 0}, {0.7, angel ? -0.5 : -0.19, 0.7},
            0, color, {5, 2, 0, actor}, {2, 0, 0, 0});
        for (int side = -1; side <= 1; side += 2) {
            add(ObjectKind::fish, MeshKind::sphere, {0.41, 0.065, side * 0.137},
                {0.057, 0.06, 0.025}, 0, {0.88F, 0.68F, 0.25F, 1}, {6, 0, 0, actor});
            add(ObjectKind::fish, MeshKind::sphere, {0.427, 0.068, side * 0.158},
                {0.026, 0.037, 0.012}, 0, {0.008F, 0.017F, 0.013F, 1}, {6, 0, 0, actor});
        }
    }
    for (std::size_t index = 0; index < 2; ++index) {
        const double x = index == 0 ? -1.8 : 2.2, z = index == 0 ? 0.9 : 0.5;
        actors_.push_back({{static_cast<float>(x), static_cast<float>(ground_height(x, z) + 0.23),
                            static_cast<float>(z), 0.8F},
                           {0.9F, 0, 0.09F, 0},
                           {},
                           {},
                           {1, 0, 0, 0}});
        const float actor = static_cast<float>(actors_.size() - 1);
        const Float4 shell{0.48F, 0.16F, 0.065F, 1};
        add(ObjectKind::crab, MeshKind::sphere, {0, 0, 0}, {0.38, 0.19, 0.3}, 0, shell,
            {6, 0, 0, actor});
        for (int side = -1; side <= 1; side += 2) {
            for (int leg = 0; leg < 4; ++leg) {
                const double offset = -0.25 + leg * 0.16;
                add(ObjectKind::crab, MeshKind::sphere, {side * 0.39, -0.02, offset},
                    {0.3, 0.033, 0.04}, side * (0.5 - leg * 0.32), shell,
                    {6, static_cast<float>(leg), 0, actor}, {3, static_cast<float>(side), 0, 0});
                add(ObjectKind::crab, MeshKind::sphere, {side * 0.61, -0.11, offset - 0.02},
                    {0.12, 0.028, 0.035}, side * 0.5, shell, {6, static_cast<float>(leg), 0, actor},
                    {3, static_cast<float>(side), 0, 0});
            }
            add(ObjectKind::crab, MeshKind::sphere, {side * 0.38, 0.02, 0.34}, {0.12, 0.10, 0.18},
                side * 0.3, {0.65F, 0.24F, 0.08F, 1}, {6, 0, 0, actor});
            add(ObjectKind::crab, MeshKind::sphere, {side * 0.14, 0.2, 0.19}, {0.035, 0.1, 0.035},
                0, shell, {6, 0, 0, actor});
            add(ObjectKind::crab, MeshKind::sphere, {side * 0.14, 0.29, 0.2}, {0.044, 0.04, 0.044},
                0, {0.008F, 0.015F, 0.01F, 1}, {6, 0, 0, actor});
        }
    }
}
void Scene::compile_batches() {
    std::vector<Instance> ordered{};
    ordered.reserve(instances_.size());
    for (std::size_t mesh = 0; mesh < batches_.size(); ++mesh) {
        Batch& batch = batches_[mesh];
        batch.mesh = meshes_[mesh];
        batch.first_instance = static_cast<std::uint32_t>(ordered.size());
        for (Object& object : objects_) {
            if (static_cast<std::size_t>(object.mesh) != mesh)
                continue;
            ordered.push_back(instances_[object.instance_index]);
            object.instance_index = static_cast<std::uint32_t>(ordered.size() - 1);
            ++batch.instance_count;
        }
    }
    instances_ = std::move(ordered);
}
TapResult Scene::tap(double x, double y, double aspect, double time) {
    return disturb(x, y, aspect, time, 0.20, 1.0, false);
}
TapResult Scene::pointer_motion(double x, double y, double aspect, double time, double speed) {
    if (!std::isfinite(speed) || speed < 0.03)
        return {};
    return disturb(x, y, aspect, time, 0.105, std::clamp(speed * 0.38, 0.12, 0.55), true);
}
TapResult Scene::disturb(double x, double y, double aspect, double time, double radius,
                        double strength, bool pointer) {
    TapResult result{};
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(time) || !std::isfinite(aspect) ||
        aspect <= 0)
        return result;
    for (std::size_t index = 0; index < actors_.size(); ++index) {
        Actor& actor = actors_[index];
        const Vec3 p = actor_position(actor, time), screen = project(p, aspect);
        const double dx = (screen.x - x) * aspect, dy = screen.y - y,
                     separation = std::hypot(dx, dy);
        if (pointer && (actor.traits.x > 0.5F || p.z < -12 ||
                        (actor.escape.w > 0 && time - actor.startled_from.w < 2.5)))
            continue;
        if (screen.z < 0 || separation > radius)
            continue;
        const double intensity = (1 - separation / radius) * strength, sign = dx >= 0 ? 1.0 : -1.0;
        const double flight = (actor.traits.x > 0.5F ? 0.5 : 2.5) * (0.5 * strength + intensity);
        Vec3 destination{std::clamp(p.x + sign * flight, -9.0, 9.0), p.y, p.z};
        if (actor.traits.x < 0.5F) {
            destination.y = std::clamp(p.y + 0.5 * intensity, 0.8, 7.5);
            destination.z -= 0.8 * intensity;
        }
        actor.startled_from = {static_cast<float>(p.x), static_cast<float>(p.y),
                               static_cast<float>(p.z), static_cast<float>(time)};
        actor.escape = {static_cast<float>(destination.x), static_cast<float>(destination.y),
                        static_cast<float>(destination.z),
                        static_cast<float>(0.6 + 0.5 * (1 - intensity))};
        actor.center.x = actor.escape.x;
        actor.center.y = actor.escape.y;
        actor.center.z = actor.escape.z;
        actor.cruise.w = 0;
        actor.cruise.x = std::min(actor.cruise.x, std::max(0.3F, 9.5F - std::abs(actor.center.x)));
        result.actors[result.count++] = static_cast<std::uint32_t>(index);
        ++statistics_.actor_edits;
        actor_revisions_[index] = statistics_.revision + 1;
    }
    if (result.count > 0)
        ++statistics_.revision;
    return result;
}
bool Scene::move_object(std::uint32_t identity, Vec3 position) {
    if (identity == 0 || identity > objects_.size() || !std::isfinite(position.x) ||
        !std::isfinite(position.y) || !std::isfinite(position.z))
        return false;
    Object& object = objects_[identity - 1];
    if (object.actor >= 0 || object.kind == ObjectKind::sand)
        return false;
    Instance& instance = instances_[object.instance_index];
    if (instance.behavior.x == 15)
        return false; // A leaf pearl is owned by its referenced plant.
    const Vec3 previous{instance.transform.values[12], instance.transform.values[13],
                        instance.transform.values[14]};
    const Vec3 delta{position.x - object.center.x, position.y - object.center.y,
                     position.z - object.center.z};
    if (delta.x == 0 && delta.y == 0 && delta.z == 0)
        return true;
    instance.transform.values[12] = static_cast<float>(previous.x + delta.x);
    instance.transform.values[13] = static_cast<float>(previous.y + delta.y);
    instance.transform.values[14] = static_cast<float>(previous.z + delta.z);
    object.center.x += delta.x;
    object.center.y += delta.y;
    object.center.z += delta.z;
    ++statistics_.revision;
    ++statistics_.instance_edits;
    instance_revisions_[object.instance_index] = statistics_.revision;
    return true;
}
QueryHit Scene::query(const Ray& ray, double time) const {
    QueryHit result{};
    result.distance = std::numeric_limits<double>::infinity();
    const double a = dot(ray.direction, ray.direction);
    if (a < 1e-20 || !std::isfinite(a) || !std::isfinite(time) || !std::isfinite(ray.origin.x) ||
        !std::isfinite(ray.origin.y) || !std::isfinite(ray.origin.z))
        return result;
    for (const Object& object : objects_) {
        if (object.kind == ObjectKind::sand)
            continue;
        Vec3 center = object.center;
        double radius = object.radius;
        if (object.actor >= 0) {
            const Actor& actor = actors_[static_cast<std::size_t>(object.actor)];
            const Vec3 at = actor_position(actor, time);
            const double elapsed =
                actor.escape.w > 0 ? time - actor.startled_from.w - actor.escape.w : time;
            const double angle = elapsed * actor.cruise.z + actor.cruise.w;
            double heading = std::atan2(0.25 * std::sin(angle), std::cos(angle));
            if (actor.escape.w > 0 && elapsed < 0)
                heading = std::atan2(-(actor.escape.z - actor.startled_from.z),
                                     actor.escape.x - actor.startled_from.x);
            if (actor.traits.x > 0.5F)
                heading = 0;
            const double c = std::cos(heading), s = std::sin(heading), scale = actor.center.w;
            center = {at.x + scale * (c * center.x + s * center.z), at.y + scale * center.y,
                      at.z + scale * (-s * center.x + c * center.z)};
            radius = (radius + 0.08) * scale;
        } else if (object.kind == ObjectKind::bubble) {
            const Instance& instance = instances_[object.instance_index];
            if (instance.behavior.x == 15) {
                const Matrix matrix = leaf_bubble_transform(instance,
                    vertices_[static_cast<std::size_t>(instance.anatomy.x)],
                    instances_[static_cast<std::size_t>(instance.anatomy.y)], time);
                center = {matrix.values[12], matrix.values[13], matrix.values[14]};
                radius = matrix.values[0];
            } else {
            const double height = std::fmod(time * instance.behavior.z + instance.behavior.y, 10.0);
            center.y += height;
            center.x += (0.025 + 0.018 * height) * std::sin(height * 2.1 + instance.behavior.y) +
                        0.04 * height;
            center.z += 0.08 * std::sin(height * 1.7 + instance.behavior.y * 2);
            radius *= 1 + 0.018 * height;
            }
        }
        const Vec3 offset = subtract(ray.origin, center);
        const double b = dot(offset, ray.direction), c = dot(offset, offset) - radius * radius,
                     d = b * b - a * c;
        if (d < 0)
            continue;
        double t = (-b - std::sqrt(d)) / a;
        if (t < 0)
            t = (-b + std::sqrt(d)) / a;
        if (t >= 0 && t < result.distance)
            result = {true, object.identity, object.kind, t};
    }
    return result;
}
} // namespace stillwater
