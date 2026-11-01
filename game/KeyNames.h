#pragma once
#include <string>

// Names of keys and mouse buttons for the key bindings (console variables in_*,
// game.cfg). Mouse buttons are codes kMouseBase + GLFW_MOUSE_BUTTON_n.
constexpr int kMouseBase = 1000;
std::string keyName(int code);          // "W", "SPACE", "MOUSE1"; "" = unknown
int keyCode(const std::string& name);   // -1 = unknown
std::string keyLabel(int code);         // for the settings screen ("Mezerník", "Levé tlačítko myši")
