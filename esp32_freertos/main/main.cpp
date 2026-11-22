// ESP-IDF 启动 FreeRTOS 后，会在主任务中调用 app_main。
// extern "C" 保留框架约定的入口符号名，函数体仍按 C++ 编译。
extern "C" void app_main()
{
    // 空入口直接返回。框架随后回收主任务，FreeRTOS 调度器继续运行。
}
