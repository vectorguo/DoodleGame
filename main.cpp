#include <vulkan/vulkan.h>
#include <iostream>
#include <stdexcept>
#include <cstdlib>

#include "DoodleRuntimeHeaders.h"

using namespace Doodle;

int main()
{
    //创建程序
    DoodleApplication app;

    //运行程序
    try
    {
        app.Initialize();
        app.Run();
        app.Destroy();
    }
    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}