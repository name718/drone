# 把入口文件注册为 main 组件的源文件，.cpp 后缀会让构建系统使用 C++ 编译器。
idf_component_register(SRCS "main.cpp" INCLUDE_DIRS "")
