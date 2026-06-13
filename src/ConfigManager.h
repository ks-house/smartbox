#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "SmartBoxController.h"

class ConfigManager {
public:
    static void loadConfig(BoxConfig& config);
    static void saveConfig(const BoxConfig& config);
};

#endif // CONFIG_MANAGER_H
