#ifndef LANDER_PARTICLE_ENGINE_H
#define LANDER_PARTICLE_ENGINE_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "ArcadeConfig.h"

class Lander_ParticleEngine {
private:
    struct Particle {
        float x, y;
        float vx, vy;
        uint16_t color;
        int lifespan;
        bool active;
    };

    static const int MAX_PARTICLES = 120;
    Particle _pool[MAX_PARTICLES];

public:
    Lander_ParticleEngine() {
        for (int i = 0; i < MAX_PARTICLES; i++) {
            _pool[i].active = false;
        }
    }

    void spawnFire(float x, float y, float vx, float vy) {
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (!_pool[i].active) {
                _pool[i].x = x;
                _pool[i].y = y;
                _pool[i].vx = vx;
                _pool[i].vy = vy;
                _pool[i].color = (random(0, 2) == 0) ? ArcadeConfig::COLOR_AMBER : 0xF800; // Amber or Red
                _pool[i].lifespan = random(10, 25);
                _pool[i].active = true;
                return;
            }
        }
    }

    void triggerExplosion(float originX, float originY, int count) {
        int particlesToSpawn = (count > MAX_PARTICLES) ? MAX_PARTICLES : count;
        for (int i = 0; i < MAX_PARTICLES && particlesToSpawn > 0; i++) {
            if (!_pool[i].active) {
                _pool[i].x = originX;
                _pool[i].y = originY;
                float angle = random(0, 360) * (PI / 180.0f);
                float speed = (random(5, 25) * 0.1f);
                
                _pool[i].vx = cos(angle) * speed;
                _pool[i].vy = sin(angle) * speed;
                
                int colorRoll = random(0, 3);
                if (colorRoll == 0) _pool[i].color = ArcadeConfig::COLOR_WHITE;
                else if (colorRoll == 1) _pool[i].color = ArcadeConfig::COLOR_AMBER;
                else _pool[i].color = 0xF800;
                
                _pool[i].lifespan = random(20, 45);
                _pool[i].active = true;
                particlesToSpawn--;
            }
        }
    }

    void update() {
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (_pool[i].active) {
                _pool[i].x += _pool[i].vx;
                _pool[i].y += _pool[i].vy;
                _pool[i].lifespan--;
                if (_pool[i].lifespan <= 0) {
                    _pool[i].active = false;
                }
            }
        }
    }

    void render(GFXcanvas16 &canvas) {
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (_pool[i].active) {
                canvas.drawPixel((int)_pool[i].x, (int)_pool[i].y, _pool[i].color);
            }
        }
    }
};

#endif