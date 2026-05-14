#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "SandboxApp.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    SandboxApp app;
    app.Run();
    return 0;
}
