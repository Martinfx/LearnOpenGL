#include "KeyNames.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

namespace {

struct Named {
    int code;
    const char* name;
    const char* label; // nullptr = same as name
};

const std::vector<Named>& table()
{
    static const std::vector<Named> t = [] {
        std::vector<Named> v = {
            {GLFW_KEY_SPACE, "SPACE", "Mezerník"},       {GLFW_KEY_LEFT_SHIFT, "LSHIFT", "Levý Shift"},
            {GLFW_KEY_RIGHT_SHIFT, "RSHIFT", "Pravý Shift"}, {GLFW_KEY_LEFT_CONTROL, "LCTRL", "Levý Ctrl"},
            {GLFW_KEY_RIGHT_CONTROL, "RCTRL", "Pravý Ctrl"}, {GLFW_KEY_LEFT_ALT, "LALT", "Levý Alt"},
            {GLFW_KEY_RIGHT_ALT, "RALT", "Pravý Alt"},    {GLFW_KEY_TAB, "TAB", "Tab"},
            {GLFW_KEY_CAPS_LOCK, "CAPSLOCK", "Caps Lock"}, {GLFW_KEY_ENTER, "ENTER", "Enter"},
            {GLFW_KEY_BACKSPACE, "BACKSPACE", "Backspace"}, {GLFW_KEY_INSERT, "INSERT", "Insert"},
            {GLFW_KEY_DELETE, "DELETE", "Delete"},        {GLFW_KEY_HOME, "HOME", "Home"},
            {GLFW_KEY_END, "END", "End"},                 {GLFW_KEY_PAGE_UP, "PGUP", "Page Up"},
            {GLFW_KEY_PAGE_DOWN, "PGDN", "Page Down"},    {GLFW_KEY_UP, "UP", "Šipka nahoru"},
            {GLFW_KEY_DOWN, "DOWN", "Šipka dolů"},        {GLFW_KEY_LEFT, "LEFT", "Šipka vlevo"},
            {GLFW_KEY_RIGHT, "RIGHT", "Šipka vpravo"},
            {kMouseBase + GLFW_MOUSE_BUTTON_LEFT, "MOUSE1", "Levé tlačítko myši"},
            {kMouseBase + GLFW_MOUSE_BUTTON_RIGHT, "MOUSE2", "Pravé tlačítko myši"},
            {kMouseBase + GLFW_MOUSE_BUTTON_MIDDLE, "MOUSE3", "Kolečko (stisk)"},
            {kMouseBase + GLFW_MOUSE_BUTTON_4, "MOUSE4", "Tlačítko myši 4"},
            {kMouseBase + GLFW_MOUSE_BUTTON_5, "MOUSE5", "Tlačítko myši 5"},
        };
        return v;
    }();
    return t;
}

} // namespace

std::string keyName(int code)
{
    if (code >= GLFW_KEY_A && code <= GLFW_KEY_Z)
        return std::string(1, static_cast<char>('A' + (code - GLFW_KEY_A)));
    if (code >= GLFW_KEY_0 && code <= GLFW_KEY_9)
        return std::string(1, static_cast<char>('0' + (code - GLFW_KEY_0)));
    if (code >= GLFW_KEY_F1 && code <= GLFW_KEY_F12)
        return "F" + std::to_string(code - GLFW_KEY_F1 + 1);
    if (code >= GLFW_KEY_KP_0 && code <= GLFW_KEY_KP_9)
        return "KP" + std::to_string(code - GLFW_KEY_KP_0);
    for (const Named& n : table())
        if (n.code == code)
            return n.name;
    return {};
}

int keyCode(const std::string& text)
{
    std::string name = text;
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z')
        return GLFW_KEY_A + (name[0] - 'A');
    if (name.size() == 1 && name[0] >= '0' && name[0] <= '9')
        return GLFW_KEY_0 + (name[0] - '0');
    if (name.size() >= 2 && name[0] == 'F' && std::isdigit(static_cast<unsigned char>(name[1]))) {
        const int n = std::atoi(name.c_str() + 1);
        if (n >= 1 && n <= 12)
            return GLFW_KEY_F1 + n - 1;
    }
    if (name.size() == 3 && name.rfind("KP", 0) == 0 && std::isdigit(static_cast<unsigned char>(name[2])))
        return GLFW_KEY_KP_0 + (name[2] - '0');
    for (const Named& n : table())
        if (name == n.name)
            return n.code;
    return -1;
}

std::string keyLabel(int code)
{
    for (const Named& n : table())
        if (n.code == code)
            return n.label ? n.label : n.name;
    const std::string name = keyName(code);
    return name.empty() ? "?" : name;
}
