/* HNN CN ETC Server launcher (calls hnn_core.dll) — Copyright (c) 2026 ETC */
#include <windows.h>
int ai_server_main(int argc, char **argv);
int main(int argc, char **argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    return ai_server_main(argc, argv);
}
