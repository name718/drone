/**
 * @file main.cpp
 * @brief 桌面自平衡机器人 · 大脑主控固件程序入口 (企业级极简规范)
 */

#include "./core/robot_brain.hpp"

/**
 * @brief 系统主入口
 */
extern "C" void app_main(void) {
    // 1. 获取机器人大脑中枢单例引用
    auto &brain = RobotBrain::getInstance();

    // 2. 执行硬件自检与各子服务初始化
    brain.init();

    // 3. 一键点火启动多核任务与交互调度引擎
    brain.start();
}
