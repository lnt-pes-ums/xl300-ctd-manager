// ctd_manager — DDS adaptor for the VALEPORT Bathy2 CTD sensor. See CtdApp.hpp
// for what CtdApp actually owns (DDS participant/topics/writers, CtdManager,
// the publish loop thread) -- this file is deliberately just orchestration:
// construct -> initialize -> start -> idle-wait for shutdown -> stop. No DDS
// code, no publish logic, nothing that needs modifying to add a new topic.
//
// Run:  ./ctd_manager ctd_config.json
#include "CtdApp.hpp"
#include "Logger.hpp"
#include "ShutdownToken.hpp"
#include "SignalHandler.hpp"

#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char **argv)
{
    ShutdownToken shutdown;
    SignalHandler::initialize(shutdown);
    Logger::init();

    if (argc < 2)
    {
        std::cerr << "usage: ctd_manager <ctd_config.json>\n";
        return 1;
    }

    CtdApp app;
    if (!app.initialize(argv[1]))
        return 1;

    app.start();
    while (!shutdown.requested())
        std::this_thread::sleep_for(std::chrono::seconds(1));
    app.stop();

    std::cout << "[ctd_manager] stopped\n";
    return 0;
}
