#include "impl/music_player.hpp"

#ifdef SYS
#include "music_control_service.hpp"

extern "C" {
extern u32 __start__;

u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;
u32 __nx_fsdev_direntry_cache_size = 1;

#define INNER_HEAP_SIZE 0x30000
size_t nx_inner_heap_size = INNER_HEAP_SIZE;
char nx_inner_heap[INNER_HEAP_SIZE];

void __libnx_initheap(void);
void __appInit(void);
void __appExit(void);

/* Exception handling. */
alignas(16) u8 __nx_exception_stack[ams::os::MemoryPageSize];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);
void __libnx_exception_handler(ThreadExceptionDump *ctx);
}

namespace ams {

    ncm::ProgramId CurrentProgramId = {0x4200000000000000};

    namespace result {

        bool CallFatalOnResultAssertion = false;

    }

}

using namespace ams;

void __libnx_initheap(void) {
    void *addr = nx_inner_heap;
    size_t size = nx_inner_heap_size;

    /* Newlib */
    extern char *fake_heap_start;
    extern char *fake_heap_end;

    fake_heap_start = (char *)addr;
    fake_heap_end = (char *)addr + size;
}

void __appInit() {
    hos::SetVersionForLibnx();

    sm::DoWithSession([] {
        R_ABORT_UNLESS(gpioInitialize());
        R_ABORT_UNLESS(pscmInitialize());
        R_ABORT_UNLESS(audoutInitialize());
        R_ABORT_UNLESS(fsInitialize());
    });

    R_ABORT_UNLESS(fsdevMountSdmc());
}

void __appExit(void) {
    fsdevUnmountAll();

    fsExit();
    audoutExit();
    pscmExit();
    gpioExit();
}

namespace {

    /* music */
    constexpr size_t NumServers = 1;
    sf::hipc::ServerManager<NumServers> g_server_manager;

    constexpr sm::ServiceName MusicServiceName = sm::ServiceName::Encode("tune");
    constexpr size_t MusicMaxSessions = 0x2;

}

int main(int argc, char *argv[]) {
    R_ABORT_UNLESS(tune::impl::Initialize());

    /* Register audio as our dependency so we can pause before it prepares for sleep. */
    u16 dependencies[] = {PscPmModuleId_Audio};

    /* Get pm module to listen for state change. */
    PscPmModule pm_module;
    R_ABORT_UNLESS(pscmGetPmModule(&pm_module, PscPmModuleId(420), dependencies, sizeof(dependencies) / sizeof(u16), true));

    /* Get GPIO session for the headphone jack pad. */
    GpioPadSession headphone_detect_session;
    R_ABORT_UNLESS(gpioOpenSession(&headphone_detect_session, GpioPadName(0x15)));

    ::Thread gpioThread;
    ::Thread pscThread;
    ::Thread audioThread;
    R_ABORT_UNLESS(threadCreate(&gpioThread, tune::impl::GpioThreadFunc, &headphone_detect_session, nullptr, 0x1000, 0x20, -2));
    R_ABORT_UNLESS(threadCreate(&pscThread, tune::impl::PscThreadFunc, &pm_module, nullptr, 0x1000, 0x20, -2));
    R_ABORT_UNLESS(threadCreate(&audioThread, tune::impl::AudioThreadFunc, nullptr, nullptr, 0x2000, 0x20, -2));

    R_ABORT_UNLESS(threadStart(&gpioThread));
    R_ABORT_UNLESS(threadStart(&pscThread));
    R_ABORT_UNLESS(threadStart(&audioThread));

    /* Create services */
    R_ABORT_UNLESS(g_server_manager.RegisterServer<tune::ControlService>(MusicServiceName, MusicMaxSessions));

    g_server_manager.LoopProcess();

    tune::impl::Exit();

    R_ABORT_UNLESS(threadWaitForExit(&gpioThread));
    R_ABORT_UNLESS(threadWaitForExit(&pscThread));
    R_ABORT_UNLESS(threadWaitForExit(&audioThread));

    R_ABORT_UNLESS(threadClose(&gpioThread));
    R_ABORT_UNLESS(threadClose(&pscThread));
    R_ABORT_UNLESS(threadClose(&audioThread));

    /* Close gpio session. */
    gpioPadClose(&headphone_detect_session);

    /* Unregister Psc module. */
    pscPmModuleFinalize(&pm_module);
    pscPmModuleClose(&pm_module);

    return 0;
}

#endif
#ifdef APPLET

namespace ams {

    ncm::ProgramId CurrentProgramId = {0x4200000000000000};

    namespace result {

        bool CallFatalOnResultAssertion = false;

    }

}

using namespace ams;

#include <unistd.h>

#ifdef R_ABORT_UNLESS
#undef R_ABORT_UNLESS
#endif

#define R_ABORT_UNLESS(res_expr)                                                                                               \
    ({                                                                                                                         \
        const auto res = static_cast<::ams::Result>((res_expr));                                                               \
        if (R_FAILED(res))                                                                                                     \
            std::printf("%s failed with 0x%x 2%03d-%04d\n", #res_expr, res.GetValue(), res.GetModule(), res.GetDescription()); \
    })

int main(int argc, char *argv[]) {
    R_ABORT_UNLESS(gpioInitialize());
    R_ABORT_UNLESS(audoutInitialize());
    R_ABORT_UNLESS(socketInitializeDefault());
    int sock = nxlinkStdio();

    R_ABORT_UNLESS(tune::impl::Initialize());

    /* Get GPIO session for the headphone jack pad. */
    GpioPadSession headphone_detect_session;
    R_ABORT_UNLESS(gpioOpenSession(&headphone_detect_session, GpioPadName(0x15)));

    os::Thread gpioThread, audioThread;
    R_ABORT_UNLESS(gpioThread.Initialize(tune::impl::GpioThreadFunc, &headphone_detect_session, 0x1000, 0x20));
    R_ABORT_UNLESS(audioThread.Initialize(tune::impl::AudioThreadFunc, nullptr, 0x8000, 0x20));

    R_ABORT_UNLESS(gpioThread.Start());
    R_ABORT_UNLESS(audioThread.Start());

    while (appletMainLoop()) {
        hidScanInput();
        u64 down = hidKeysDown(CONTROLLER_P1_AUTO);
        if (down & KEY_A) {
            ams::Result rc = tune::impl::Play();
            std::printf("Play: 0x%x 2%03d-%04d\n", rc.GetValue(), rc.GetModule(), rc.GetDescription());
        }

        if (down & KEY_B) {
            ams::Result rc = tune::impl::Pause();
            std::printf("Pause: 0x%x 2%03d-%04d\n", rc.GetValue(), rc.GetModule(), rc.GetDescription());
        }

        constexpr const char path[] = "/music/misc/Dj CUTMAN - The Legend of Dubstep.mp3";
        if (down & KEY_MINUS) {
            ams::Result rc = tune::impl::Enqueue(path, strlen(path), tune::EnqueueType::Last);
            std::printf("Enqueue: 0x%x 2%03d-%04d\n", rc.GetValue(), rc.GetModule(), rc.GetDescription());
        }

        if (down & KEY_PLUS)
            break;

        if (down & KEY_RIGHT)
            tune::impl::Next();

        if (down & KEY_LEFT)
            tune::impl::Prev();
    }

    tune::impl::Exit();

    R_ABORT_UNLESS(gpioThread.Wait());
    R_ABORT_UNLESS(audioThread.Wait());

    R_ABORT_UNLESS(gpioThread.Join());
    R_ABORT_UNLESS(audioThread.Join());

    /* Close gpio session. */
    gpioPadClose(&headphone_detect_session);

    close(sock);
    socketExit();
    audoutExit();
    gpioExit();
}

#endif
