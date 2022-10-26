#include "state.h"
#include <coreinit/launch.h>

typedef enum {
    APP_STATE_STOPPING = 0,
    APP_STATE_STOPPED,
    APP_STATE_RUNNING,
    APP_STATE_BACKGROUND,
    APP_STATE_RETURNING,
} APP_STATE;

static volatile APP_STATE app = APP_STATE_RUNNING;

static uint32_t homeButtonCallback(void *dummy) {
    app = APP_STATE_STOPPING;
    return 0;
}

bool AppRunning() {
    if (OSIsMainCore() && app != APP_STATE_STOPPED) {
        switch (ProcUIProcessMessages(true)) {
            case PROCUI_STATUS_EXITING:
                // Being closed, prepare to exit
                app = APP_STATE_STOPPED;
                break;
            case PROCUI_STATUS_RELEASE_FOREGROUND:
                // Free up MEM1 to next foreground app, deinit screen, etc.
                ProcUIDrawDoneRelease();
                if (app != APP_STATE_STOPPING)
                    app = APP_STATE_BACKGROUND;
                break;
            case PROCUI_STATUS_IN_FOREGROUND:
                // Executed while app is in foreground
                if (app == APP_STATE_STOPPING)
                    break;
                if (app == APP_STATE_BACKGROUND) {
                    app = APP_STATE_RETURNING;
                } else
                    app = APP_STATE_RUNNING;

                break;
            case PROCUI_STATUS_IN_BACKGROUND:
                if (app != APP_STATE_STOPPING)
                    app = APP_STATE_BACKGROUND;
                break;
        }
    }

    return app;
}

void initState() {
    ProcUIRegisterCallback(PROCUI_CALLBACK_HOME_BUTTON_DENIED, &homeButtonCallback, NULL, 100);
    OSEnableHomeButtonMenu(false);
}

void shutdownState() {
    OSForceFullRelaunch();
    SYSLaunchMenu();
    while (app != APP_STATE_STOPPED) {
        AppRunning();
    }
}