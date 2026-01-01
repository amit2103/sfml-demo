#include <SFML/Graphics.hpp>
#include <cmath>
#include <cstdint>
#include <deque>
#include <random>
#include <string>
#include <vector>

static constexpr float PI = 3.14159265358979323846f;

static float wrap2pi(float a) {
    float x = std::fmod(a, 2.f * PI);
    if (x < 0) x += 2.f * PI;
    return x;
}

static sf::Vector2f rotateVec(sf::Vector2f v, float rad) {
    float c = std::cos(rad), s = std::sin(rad);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

// Solve Kepler's equation: E - e*sin(E) = M  (Newton-Raphson)
static float solveEccentricAnomaly(float M, float e) {
    M = wrap2pi(M);

    // Good starting guess
    float E = (e < 0.8f) ? M : PI;

    for (int i = 0; i < 8; i++) {
        float f  = E - e * std::sin(E) - M;
        float fp = 1.f - e * std::cos(E);
        E -= f / fp;
    }
    return E;
}

// Position in pixels around a focus at origin (Sun), using a (semi-major), e (eccentricity).
// In orbital plane, focus at (0,0):
// x = a (cosE - e), y = a * sqrt(1-e^2) sinE
static sf::Vector2f keplerOrbitPos(float aPx, float e, float periodDays, float tDays, float phase0Rad) {
    float n = 2.f * PI / periodDays;          // mean motion (rad/day)
    float M = phase0Rad + n * tDays;          // mean anomaly
    float E = solveEccentricAnomaly(M, e);

    float x = aPx * (std::cos(E) - e);
    float y = aPx * (std::sqrt(1.f - e * e) * std::sin(E));
    return {x, y};
}

struct Trail {
    std::deque<sf::Vector2f> pts;
    std::size_t maxLen = 260;

    void push(sf::Vector2f p) {
        if (!pts.empty()) {
            // avoid adding nearly identical points (reduces noise)
            auto last = pts.back();
            float dx = p.x - last.x, dy = p.y - last.y;
            if (dx*dx + dy*dy < 0.5f*0.5f) return;
        }
        pts.push_back(p);
        while (pts.size() > maxLen) pts.pop_front();
    }

    void draw(sf::RenderTarget& target, sf::Color baseColor) const {
        if (pts.size() < 2) return;

        sf::VertexArray va(sf::PrimitiveType::LineStrip, pts.size());
        const float N = static_cast<float>(pts.size());

        for (std::size_t i = 0; i < pts.size(); i++) {
            va[i].position = pts[i];

            // fade from older -> newer
            float t = (N <= 1.f) ? 1.f : static_cast<float>(i) / (N - 1.f);
            // alpha ramp: older faint, newer brighter
            std::uint8_t a = static_cast<std::uint8_t>(40 + t * 180);
            va[i].color = sf::Color(baseColor.r, baseColor.g, baseColor.b, a);
        }

        target.draw(va);
    }
};

struct Planet {
    std::string name;
    sf::Color color;

    float radiusPx;   // visual body radius
    float aPx;        // semi-major axis (visual)
    float e;          // eccentricity
    float periodDays; // orbital period

    float orbitRotRad; // rotate orbital plane (visual)
    float phase0Rad;   // initial anomaly offset

    sf::Vector2f pos;  // computed each frame
    Trail trail;
};

static std::vector<sf::Vector2f> makeStars(int count, sf::Vector2u winSize, uint32_t seed = 42) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dx(0.f, static_cast<float>(winSize.x));
    std::uniform_real_distribution<float> dy(0.f, static_cast<float>(winSize.y));
    std::vector<sf::Vector2f> stars;
    stars.reserve(count);
    for (int i = 0; i < count; i++) stars.push_back({dx(rng), dy(rng)});
    return stars;
}

// Draw an orbit curve by sampling E from 0..2π (focus at sun)
static void drawOrbit(sf::RenderTarget& target, sf::Vector2f center, float aPx, float e, float rotRad, float zoom) {
    constexpr int SAMPLES = 240;
    sf::VertexArray va(sf::PrimitiveType::LineStrip, SAMPLES + 1);

    for (int i = 0; i <= SAMPLES; i++) {
        float E = 2.f * PI * (static_cast<float>(i) / SAMPLES);
        float x = aPx * (std::cos(E) - e);
        float y = aPx * (std::sqrt(1.f - e * e) * std::sin(E));

        sf::Vector2f p = rotateVec({x, y}, rotRad);
        p *= zoom;
        p += center;

        va[i].position = p;
        va[i].color = sf::Color(80, 80, 110, 130);
    }

    target.draw(va);
}



int main() {
    sf::RenderWindow window(sf::VideoMode({1200, 800}), "SFML Solar System (Kepler-ish)");
    window.setFramerateLimit(60);

    const sf::Vector2f center{window.getSize().x * 0.5f, window.getSize().y * 0.5f};
    auto stars = makeStars(700, window.getSize(), 7);

    // Simulation controls
    float simDaysPerSecond = 10.f; // increase/decrease with Up/Down
    bool paused = false;
    float zoom = 1.0f;

    float simTimeDays = 0.f;

    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> rnd0(0.f, 2.f * PI);
    auto rphase = [&]() { return rnd0(rng); };

    // Planets: (visual sizes exaggerated; a/e/period are roughly realistic ratios)
    std::vector<Planet> planets = {
        {"Mercury", sf::Color(180,180,180), 4.f,   80.f,  0.2056f,   88.f,     0.3f, rphase()},
        {"Venus",   sf::Color(220,190,120), 7.f,  110.f,  0.0067f,  224.7f,   0.8f, rphase()},
        {"Earth",   sf::Color( 80,140,255), 7.f,  145.f,  0.0167f,  365.25f,  1.1f, rphase()},
        {"Mars",    sf::Color(220, 90, 70), 6.f,  190.f,  0.0934f,  687.f,    1.4f, rphase()},
        {"Jupiter", sf::Color(214, 182, 142), 12.f, 260.f, 0.0489f, 4332.6f, 1.8f, rphase()},
        {"Saturn",  sf::Color(210,190,120), 10.f, 340.f,  0.0565f, 10759.2f,  2.2f, rphase()},
        {"Uranus",  sf::Color(140,220,220), 9.f,  420.f,  0.0457f, 30688.5f,  2.7f, rphase()},
        {"Neptune", sf::Color( 80,120,255), 9.f,  500.f,  0.0113f, 60182.f,   3.1f, rphase()}
    };

    // Trails: longer for inner planets looks nicer; tweak if you want
    for (auto& p : planets) p.trail.maxLen = 280;

    // Moon orbit around Earth (relative orbit):
    // (visual) a=22 px, e small, period ~27.3 days
    Planet moon{
        "Moon", sf::Color(210, 210, 220),
        3.f, 22.f, 0.055f, 27.3f,
        0.f, rphase()
    };
    moon.trail.maxLen = 220;

    sf::Clock clock;

    while (window.isOpen()) {
        // --- Events ---
        while (auto ev = window.pollEvent()) {
            if (ev->is<sf::Event::Closed>()) window.close();

            if (ev->is<sf::Event::KeyPressed>()) {
                auto key = ev->getIf<sf::Event::KeyPressed>()->code;

                if (key == sf::Keyboard::Key::Escape) window.close();
                if (key == sf::Keyboard::Key::Space) paused = !paused;

                if (key == sf::Keyboard::Key::Up)   simDaysPerSecond *= 1.25f;
                if (key == sf::Keyboard::Key::Down) simDaysPerSecond /= 1.25f;

                if (key == sf::Keyboard::Key::Right) zoom *= 1.1f;
                if (key == sf::Keyboard::Key::Left)  zoom /= 1.1f;

                if (simDaysPerSecond < 0.05f) simDaysPerSecond = 0.05f;
                if (simDaysPerSecond > 20000.f) simDaysPerSecond = 20000.f;
                if (zoom < 0.3f) zoom = 0.3f;
                if (zoom > 3.0f) zoom = 3.0f;
            }
        }

        // --- Update ---
        float dt = clock.restart().asSeconds();
        if (!paused) {
            simTimeDays += dt * simDaysPerSecond;
        }

        // Compute planet positions (Sun focus at origin -> then rotate + zoom + translate)
        for (auto& p : planets) {
            sf::Vector2f local = keplerOrbitPos(p.aPx, p.e, p.periodDays, simTimeDays, p.phase0Rad);
            local = rotateVec(local, p.orbitRotRad);
            p.pos = center + local * zoom;
            p.trail.push(p.pos);
        }

         // Moon position relative to Earth (find Earth)
sf::Vector2f earthPos{};
float earthRadiusPx = 7.f; // must match Earth's radiusPx above

for (auto& p : planets) {
    if (p.name == "Earth") { earthPos = p.pos; break; }
}

{
    sf::Vector2f localMoon = keplerOrbitPos(moon.aPx, moon.e, moon.periodDays, simTimeDays, moon.phase0Rad);
    localMoon = rotateVec(localMoon, 0.7f);

    // Apply zoom consistently (same as planets)
    sf::Vector2f offset = localMoon * zoom;

    // --- prevent visual overlap at small zoom ---
    float r2 = offset.x * offset.x + offset.y * offset.y;
    float r  = std::sqrt(r2);

    float minR = earthRadiusPx + moon.radiusPx + 6.f;  // padding
    if (r < minR) {
        float scale = minR / (r + 1e-6f);
        offset *= scale;
    }
    // -------------------------------------------

    moon.pos = earthPos + offset;
    moon.trail.push(moon.pos);
}


        // --- Draw ---
        window.clear(sf::Color(5, 5, 12));

        // Stars
        {
            sf::VertexArray va(sf::PrimitiveType::Points, stars.size());
            for (std::size_t i = 0; i < stars.size(); i++) {
                va[i].position = stars[i];
                va[i].color = sf::Color(200, 200, 220);
            }
            window.draw(va);
        }

        // Sun
        {
            sf::CircleShape sun(20.f);
            sun.setFillColor(sf::Color(255, 210, 60));
            sun.setOrigin({20.f, 20.f});
            sun.setPosition(center);
            window.draw(sun);

            sf::CircleShape glow(36.f);
            glow.setFillColor(sf::Color(255, 210, 60, 40));
            glow.setOrigin({36.f, 36.f});
            glow.setPosition(center);
            window.draw(glow);
        }

        // Orbits (elliptical)
        for (const auto& p : planets) {
            drawOrbit(window, center, p.aPx, p.e, p.orbitRotRad, zoom);
        }

        // Trails (draw first, then bodies on top)
        for (const auto& p : planets) p.trail.draw(window, p.color);
        moon.trail.draw(window, moon.color);

        // Bodies
        for (const auto& p : planets) {
            sf::CircleShape body(p.radiusPx);
            body.setFillColor(p.color);
            body.setOrigin({p.radiusPx, p.radiusPx});
            body.setPosition(p.pos);
            window.draw(body);

            // Saturn ring (simple ellipse)
            if (p.name == "Saturn") {
                // ring as a scaled circle (ellipse)
                sf::CircleShape ring(1.f, 120);
                ring.setOrigin({1.f, 1.f});
                ring.setPosition(p.pos);
                ring.setRotation(sf::degrees(-20.f)); // tilt
                ring.setScale({24.f, 10.f});          // ellipse radii
                ring.setFillColor(sf::Color::Transparent);
                ring.setOutlineThickness(1.6f);
                ring.setOutlineColor(sf::Color(220, 210, 170, 170));
                window.draw(ring);

                // inner ring
                sf::CircleShape ring2(1.f, 120);
                ring2.setOrigin({1.f, 1.f});
                ring2.setPosition(p.pos);
                ring2.setRotation(sf::degrees(-20.f));
                ring2.setScale({18.f, 7.f});
                ring2.setFillColor(sf::Color::Transparent);
                ring2.setOutlineThickness(1.2f);
                ring2.setOutlineColor(sf::Color(180, 170, 140, 140));
                window.draw(ring2);
            }
        }

        // Moon
        {
            sf::CircleShape m(moon.radiusPx);
            m.setFillColor(moon.color);
            m.setOrigin({moon.radiusPx, moon.radiusPx});
            m.setPosition(moon.pos);
            window.draw(m);
        }

        window.display();
    }

    return 0;
}
