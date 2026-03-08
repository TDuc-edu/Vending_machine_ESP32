/**
 * @file    recipe_engine.cpp
 * @brief   Recipe engine implementation with default presets
 */

#include "recipe_engine.h"

/* ============================================================================
 *                         DEFAULT RECIPES
 * ============================================================================ */

static recipe_t recipes[MAX_RECIPES] = {
    // BTN_0: Hold mode — pump runs while button is held
    {
        .mode          = RECIPE_MODE_HOLD,
        .volume_ml     = 0,
        .speed_percent = 80,
        .name          = "HOLD"
    },
    // BTN_1: Dispense 100ml
    {
        .mode          = RECIPE_MODE_VOLUME,
        .volume_ml     = 100,
        .speed_percent = 80,
        .name          = "100ml"
    },
    // BTN_2: Dispense 250ml
    {
        .mode          = RECIPE_MODE_VOLUME,
        .volume_ml     = 250,
        .speed_percent = 80,
        .name          = "250ml"
    }
};

static bool is_initialized = false;

/* ============================================================================
 *                         PUBLIC FUNCTIONS
 * ============================================================================ */

void recipe_engine_init(void)
{
    is_initialized = true;
    Serial.println("[RECIPE] Engine initialized");
    recipe_engine_print_all();
}

const recipe_t* recipe_engine_get(uint8_t button_index)
{
    if (button_index >= MAX_RECIPES) return NULL;
    return &recipes[button_index];
}

bool recipe_engine_set(uint8_t button_index, const recipe_t* recipe)
{
    if (button_index >= MAX_RECIPES || recipe == NULL) return false;

    recipes[button_index].mode          = recipe->mode;
    recipes[button_index].volume_ml     = recipe->volume_ml;
    recipes[button_index].speed_percent = recipe->speed_percent;
    recipes[button_index].name          = recipe->name;

    Serial.printf("[RECIPE] Updated BTN_%d: %s, %luml, %d%%\n",
                  button_index, recipe->name, recipe->volume_ml, recipe->speed_percent);
    return true;
}

void recipe_engine_set_volume(uint8_t button_index, uint32_t volume_ml)
{
    if (button_index >= MAX_RECIPES) return;
    if (recipes[button_index].mode != RECIPE_MODE_VOLUME) return;

    recipes[button_index].volume_ml = volume_ml;
    Serial.printf("[RECIPE] BTN_%d volume: %luml\n", button_index, volume_ml);
}

void recipe_engine_set_speed(uint8_t button_index, uint8_t speed_percent)
{
    if (button_index >= MAX_RECIPES) return;
    if (speed_percent > 100) speed_percent = 100;

    recipes[button_index].speed_percent = speed_percent;
    Serial.printf("[RECIPE] BTN_%d speed: %d%%\n", button_index, speed_percent);
}

void recipe_engine_print_all(void)
{
    Serial.println("[RECIPE] Presets:");
    for (int i = 0; i < MAX_RECIPES; i++) {
        if (recipes[i].mode == RECIPE_MODE_HOLD) {
            Serial.printf("  BTN_%d: %s (HOLD mode) @ %d%%\n",
                          i, recipes[i].name, recipes[i].speed_percent);
        } else {
            Serial.printf("  BTN_%d: %s (%luml) @ %d%%\n",
                          i, recipes[i].name, recipes[i].volume_ml,
                          recipes[i].speed_percent);
        }
    }
}
