#include "console.h"
#include "kernel.h"

int main() {
    Console::init();

    Kernel kernel;
    kernel.boot();
    kernel.run();

    return 0;
}