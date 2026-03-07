/**
 * @file    recipe_engine.h
 * @brief   Recipe/preset configuration engine for vending machine
 * @details Manages drink presets (volume, speed, name).
 *          Each button maps to a recipe.
 */

#ifndef RECIPE_ENGINE_H
#define RECIPE_ENGINE_H

#include <Arduino.h>

/* ============================================================================
 *                         RECIPE DEFINITIONS
 * ============================================================================ */

#define MAX_RECIPES     3

typedef enum {
    RECIPE_MODE_HOLD = 0,       // Pump while button held
    RECIPE_MODE_VOLUME          // Pump until target volume reached
} recipe_mode_t;

typedef struct {
    recipe_mode_t mode;
    uint32_t      volume_ml;        // Target volume (only for VOLUME mode)
    uint8_t       speed_percent;    // Pump speed (0–100)
    const char*   name;             // Human-readable name
} recipe_t;

/* ============================================================================
 *                         FUNCTION PROTOTYPES
 * ============================================================================ */

/**
 * @brief Initialize recipe engine with default presets
 */
void recipe_engine_init(void);

/**
 * @brief Get recipe for a button index
 * @param button_index 0–4
 * @return Pointer to recipe (NULL if invalid)
 */
const recipe_t* recipe_engine_get(uint8_t button_index);

/**
 * @brief Update a recipe
 * @param button_index 0–4
 * @param recipe New recipe configuration
 * @return true if updated
 */
bool recipe_engine_set(uint8_t button_index, const recipe_t* recipe);

/**
 * @brief Set volume for a specific preset
 * @param button_index 0–4
 * @param volume_ml Volume in ml
 */
void recipe_engine_set_volume(uint8_t button_index, uint32_t volume_ml);

/**
 * @brief Set speed for a specific preset
 * @param button_index 0–4
 * @param speed_percent 0–100
 */
void recipe_engine_set_speed(uint8_t button_index, uint8_t speed_percent);

/**
 * @brief Print all recipes to Serial
 */
void recipe_engine_print_all(void);

#endif // RECIPE_ENGINE_H
