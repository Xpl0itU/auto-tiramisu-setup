#include <curl/curl.h>

#include <coreinit/memheap.h>
#include <sysapp/launch.h>

#include <whb/proc.h>
#include <whb/log.h>
#include <whb/log_console.h>

#include <romfs-wiiu.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "input.h"
#include "state.h"

#include "kernel.h"
#include "zip/zip.h"

#define ARRAY_LENGTH(array) (sizeof((array)) / sizeof((array)[0]))
#define IO_BUFSIZE	(128 * 1024) // 128 KB

const char *skip_file_list[] = {
	"manifest.install",
	"info.json",
    "versions.json",
    "screen1.png",
    "screen2.png",
    "src"
};

static int cursorPos = 0;

extern "C" void __init_wut_malloc();

// Initialize correct heaps for CustomRPXLoader
extern "C" void __preinit_user(MEMHeapHandle *outMem1, MEMHeapHandle *outFG, MEMHeapHandle *outMem2) {
    __init_wut_malloc();
}

static inline void drawToScreen(const char* text) {
    WHBLogPrint(text);
    WHBLogConsoleDraw();
}

static int initSocket(void *ptr, curl_socket_t socket, curlsocktype type) {
	int o = 1;

	// Activate WinScale
	int r = setsockopt(socket, SOL_SOCKET, SO_WINSCALE, &o, sizeof(o));
	if(r != 0) {
		WHBLogPrintf("initSocket: Error setting WinScale: %d", r);
		return CURL_SOCKOPT_ERROR;
	}

	//Activate TCP SAck
	r = setsockopt(socket, SOL_SOCKET, SO_TCPSACK, &o, sizeof(o));
	if(r != 0) {
		WHBLogPrintf("initSocket: Error setting TCP SAck: %d", r);
		return CURL_SOCKOPT_ERROR;
	}

	// Disable slowstart. Should be more important fo a server but doesn't hurt a client, too
	r = setsockopt(socket, SOL_SOCKET, 0x4000, &o, sizeof(o));
	if(r != 0) {
		WHBLogPrintf("initSocket: Error setting Noslowstart: %d", r);
		return CURL_SOCKOPT_ERROR;
	}

	o = 0;
	// Disable TCP keepalive - libCURL default
	r = setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, &o, sizeof(o));
	if(r != 0) {
		WHBLogPrintf("initSocket: Error setting TCP nodelay: %d", r);
		return CURL_SOCKOPT_ERROR;
	}

	o = IO_BUFSIZE;
	// Set receive buffersize
	r = setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &o, sizeof(o));
	if(r != 0) {
		WHBLogPrintf("initSocket: Error setting RBS: %d", r);
		return CURL_SOCKOPT_ERROR;
	}

	return CURL_SOCKOPT_OK;
}

static size_t writefunction(void *ptr, size_t size, size_t nmemb, void *stream) {
    size_t written = fwrite(ptr, size, nmemb, (FILE *)stream);
    return written;
}

static int make_file_path(const char* name) {
	int err = 0;
	char *_name = strdup(name), *p;
	for (p = strchr(_name + 1, '/'); p; p = strchr(p + 1, '/')) {
		*p = '\0';
		err = mkdir(_name, 0775) == -1;
		err = err && (errno != EEXIST);
		if (err)
			break;
		*p = '/';
	}
	free(_name);
	return !err;
}

static void extract_package(const char *path) {
	struct zip_t *zip = zip_open(path, 0, 'r');
	for (int i = 0; i < zip_total_entries(zip); i++) {
		zip_entry_openbyindex(zip, i);
		
		// get file name
		const char *name = zip_entry_name(zip);
	
		// check if the file should be skipped
		int skip = 0;
		for (unsigned int j = 0; j < ARRAY_LENGTH(skip_file_list); j++)
			skip |= strcmp(name, skip_file_list[j]) == 0;
		
		// extract the file
		if (!skip && make_file_path(name))
			zip_entry_fread(zip, name);
		
		zip_entry_close(zip);
	}
	zip_close(zip);
}

static int downloadFile(const char* url, const char* path, const char* cert) {
    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK) {
        WHBLogPrintf("curl_global_init: %d", res);
        return 1;
    }

    // Start a curl session
    CURL* curl = curl_easy_init();
    if (!curl) {
        drawToScreen("curl_easy_init: failed");
        curl_global_cleanup();
        return 1;
    }

    // Use the certificate bundle in the romfs
    curl_easy_setopt(curl, CURLOPT_CAINFO, cert);

    // Enable optimizations
    curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, initSocket);

    // Follow redirects
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    // Set the custom write function
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunction);

    // Set the download URL
    curl_easy_setopt(curl, CURLOPT_URL, url);

    drawToScreen("Starting download...");

    // Perform the download
    FILE *file = fopen(path, "wb");
    if(file) {
        // write the page body to this file handle
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);

        drawToScreen("Writing...");
        // get it!
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            WHBLogPrintf("curl_easy_perform: %d", res);
            return 1;
        }

        // close the header file
        fclose(file);
    }

    // Done, clean up and exit
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return 0;
}

static inline void drawHeader() {
    WHBLogPrint("Automatic Wii U Homebrew Setup");
    WHBLogPrint("");
}

#define NUM_LINES (16)

static void clearScreen() {
    for (int i = 0; i < NUM_LINES; i++)
        WHBLogPrint("");
}

int main() {
    // Initialize ProcUI
    WHBProcInit();
    initState();

    // Initialize a log console
    WHBLogConsoleInit();

    // Initialize romfs for the certificate bundle
    romfsInit();
    Input input;
    while (AppRunning()) {
        input.read();
        clearScreen();
        drawHeader();
        WHBLogPrintf("%c Download Tiramisu", cursorPos == 0 ? '>' : ' ');
        WHBLogPrintf("%c Download vWii Homebrew files", cursorPos == 1 ? '>' : ' ');
        WHBLogPrintf("%c Download Aroma", cursorPos == 2 ? '>' : ' ');
        WHBLogConsoleDraw();
        if(input.get(TRIGGER, PAD_BUTTON_DOWN) && cursorPos != 2)
            cursorPos++;
        if(input.get(TRIGGER, PAD_BUTTON_UP) && cursorPos != 0)
            cursorPos--;
        if(input.get(TRIGGER, PAD_BUTTON_A))
            break;
    }

    if((cursorPos == 0) && input.get(TRIGGER, PAD_BUTTON_A)) {
        drawToScreen("Downloading Tiramisu...");

        if(downloadFile("https://github.com/wiiu-env/Tiramisu/releases/download/v0.1/environmentloader-7194938+wiiu-nanddumper-payload-5c5ec09+fw_img_loader-c2da326+payloadloaderinstaller-98367a9+tiramisu-7b881d3.zip", "/vol/external01/tiramisu.zip", "romfs:/github-com.pem") == 1) {
            drawToScreen("Error while downloading Tiramisu");
            goto done;
        }
        
        drawToScreen("Extracting Tiramisu...");

        extract_package("/vol/external01/tiramisu.zip");

        drawToScreen("Downloading Sigpatches...");

        if(downloadFile("https://github.com/marco-calautti/SigpatchesModuleWiiU/releases/latest/download/01_sigpatches.rpx", "/vol/external01/wiiu/environments/tiramisu/modules/setup/01_sigpatches.rpx", "romfs:/github-com.pem") == 1) {
            drawToScreen("Error while downloading Sigpatches");
            goto done;
        }

        drawToScreen("Downloading Homebrew App Store...");

        if(downloadFile("http://wiiubru.com/appstore/zips/appstore.zip", "/vol/external01/appstore.zip", "romfs:/wiiubru-com.pem") == 1) {
            drawToScreen("Error while downloading Homebrew App Store");
            goto done;
        }
        
        drawToScreen("Extracting Homebrew App Store...");

        extract_package("/vol/external01/appstore.zip");

        drawToScreen("Downloading SaveMii Mod WUT Port...");

        if(downloadFile("https://wiiu.cdn.fortheusers.org/zips/SaveMiiModWUTPort.zip", "/vol/external01/savemii.zip", "romfs:/wiiubru-com.pem") == 1) {
            drawToScreen("Error while downloading SaveMii Mod WUT Port");
            goto done;
        }
        
        drawToScreen("Extracting SaveMii Mod WUT Port...");

        extract_package("/vol/external01/savemii.zip");

        drawToScreen("Cleaning files...");

        remove("/vol/external01/tiramisu.zip");
        remove("/vol/external01/appstore.zip");
        remove("/vol/external01/savemii.zip");
    } else if((cursorPos == 1) && input.get(TRIGGER, PAD_BUTTON_A)) {
        drawToScreen("Downloading Tiramisu...");

        if(downloadFile("https://github.com/wiiu-env/Tiramisu/releases/download/v0.1/environmentloader-7194938+wiiu-nanddumper-payload-5c5ec09+fw_img_loader-c2da326+payloadloaderinstaller-98367a9+tiramisu-7b881d3.zip", "/vol/external01/tiramisu.zip", "romfs:/github-com.pem") == 1) {
            drawToScreen("Error while downloading Tiramisu");
            goto done;
        }
        
        drawToScreen("Extracting Tiramisu...");

        extract_package("/vol/external01/tiramisu.zip");

        drawToScreen("Downloading compat-installer...");

        if(downloadFile("https://github.com/Xpl0itU/vwii-compat-installer/releases/download/v1.2/compat_installer.rpx", "/vol/external01/wiiu/apps/compat-installer.rpx", "romfs:/github-com.pem") == 1) {
            drawToScreen("Error while downloading compat-installer");
            goto done;
        }

        drawToScreen("Downloading Patched IOS 80 Installer for vWii...");

        if(downloadFile("https://wiiu.hacks.guide/docs/files/Patched_IOS80_Installer_for_vWii.zip", "/vol/external01/Patched_IOS80_Installer_for_vWii.zip", "romfs:/wiiu-hacks-guide.pem") == 1) {
            drawToScreen("Error while downloading Patched IOS 80 Installer for vWii");
            goto done;
        }
        
        drawToScreen("Extracting Patched IOS 80 Installer for vWii...");

        extract_package("/vol/external01/Patched_IOS80_Installer_for_vWii.zip");

        drawToScreen("Downloading d2x cIOS Installer...");

        if(downloadFile("https://wiiu.hacks.guide/docs/files/d2x_cIOS_Installer.zip", "/vol/external01/d2x_cIOS_Installer.zip", "romfs:/wiiu-hacks-guide.pem") == 1) {
            drawToScreen("Error while downloading d2x cIOS Installer");
            goto done;
        }
        
        drawToScreen("Extracting d2x cIOS Installer...");

        extract_package("/vol/external01/d2x_cIOS_Installer.zip");

        drawToScreen("Cleaning files...");

        remove("/vol/external01/tiramisu.zip");
        remove("/vol/external01/Patched_IOS80_Installer_for_vWii.zip");
        remove("/vol/external01/d2x_cIOS_Installer.zip");
    } else if((cursorPos == 2) && input.get(TRIGGER, PAD_BUTTON_A)) {
        bool nandDumperSelected = false, fwimgloaderSelected = false;
        bool bloopairSelected = false, wiiloadSelected = false;
        bool ftpiiuSelected = false, sdcafiineSelected = false;
        bool usbSerialLoggingSelected = false;
        cursorPos = 0;
        while (AppRunning()) {
            input.read();
            clearScreen();

            // Payloads
            WHBLogPrintf("%c [%c] Nanddumper", cursorPos == 0 ? '>' : ' ', nandDumperSelected ? 'x' : ' ');
            WHBLogPrintf("%c [%c] fw.img loader", cursorPos == 1 ? '>' : ' ', fwimgloaderSelected ? 'x' : ' ');
            
            // Plugins and modules
            WHBLogPrintf("%c [%c] Bloopair", cursorPos == 2 ? '>' : ' ', bloopairSelected ? 'x' : ' ');
            WHBLogPrintf("%c [%c] Wiiload Plugin", cursorPos == 3 ? '>' : ' ', wiiloadSelected ? 'x' : ' ');
            WHBLogPrintf("%c [%c] FTPiiU Plugin", cursorPos == 4 ? '>' : ' ', ftpiiuSelected ? 'x' : ' ');
            WHBLogPrintf("%c [%c] SDCafiine Plugin", cursorPos == 5 ? '>' : ' ', sdcafiineSelected ? 'x' : ' ');
            WHBLogPrintf("%c [%c] USB Serial logging", cursorPos == 6 ? '>' : ' ', usbSerialLoggingSelected ? 'x' : ' ');

            WHBLogPrintf("");
            
            drawToScreen("(A) Select (+) Start Download");
            
            if(input.get(TRIGGER, PAD_BUTTON_DOWN) && cursorPos != 6)
                cursorPos++;
            if(input.get(TRIGGER, PAD_BUTTON_UP) && cursorPos != 0)
                cursorPos--;
            if(input.get(TRIGGER, PAD_BUTTON_A)) {
                switch (cursorPos) {
                    case 0:
                        nandDumperSelected = !nandDumperSelected;
                        break;
                    case 1:
                        fwimgloaderSelected = !fwimgloaderSelected;
                        break;
                    case 2:
                        bloopairSelected = !bloopairSelected;
                        break;
                    case 3:
                        wiiloadSelected = !wiiloadSelected;
                        break;
                    case 4:
                        ftpiiuSelected = !ftpiiuSelected;
                        break;
                    case 5:
                        sdcafiineSelected = !sdcafiineSelected;
                        break;
                    case 6:
                        usbSerialLoggingSelected = !usbSerialLoggingSelected;
                        break;
                    default:
                        break;
                }
            }
            if(input.get(TRIGGER, PAD_BUTTON_PLUS))
                break;
        }
        drawToScreen("Downloading Payloads...");

        char url[1024];
        sprintf(url, "https://aroma.foryour.cafe/api/download?packages=environmentloader%s%s", nandDumperSelected ? ",wiiu-nanddumper-payload" : "", fwimgloaderSelected ? ",fw_img_loader" : "");

        if(downloadFile(url, "/vol/external01/payloads.zip", "romfs:/foryour-cafe.pem") == 1) {
            drawToScreen("Error while downloading Payloads");
            goto done;
        }
        
        drawToScreen("Extracting Payloads...");

        extract_package("/vol/external01/payloads.zip");

        drawToScreen("Downloading Base Aroma...");

        if(downloadFile("https://aroma.foryour.cafe/api/download?packages=base-aroma", "/vol/external01/base.zip", "romfs:/foryour-cafe.pem") == 1) {
            drawToScreen("Error while downloading Base Aroma");
            goto done;
        }
        
        drawToScreen("Extracting Base Aroma...");

        extract_package("/vol/external01/base.zip");

        drawToScreen("Downloading Plugins and Modules...");

        sprintf(url, "https://aroma.foryour.cafe/api/download?packages=%s%s%s%s%s", bloopairSelected ? "bloopair" : "", wiiloadSelected ? ",wiiload" : "", ftpiiuSelected ? ",ftpiiu" : "", sdcafiineSelected ? ",sdcafiine" : "", usbSerialLoggingSelected ? ",usbseriallogger" : "");

        if(downloadFile(url, "/vol/external01/plugins.zip", "romfs:/foryour-cafe.pem") == 1) {
            drawToScreen("Error while downloading Plugins and Modules");
            goto done;
        }
        
        drawToScreen("Extracting Plugins and Modules...");

        extract_package("/vol/external01/plugins.zip");

        drawToScreen("Downloading Sigpatches...");

        if(downloadFile("https://github.com/marco-calautti/SigpatchesModuleWiiU/releases/latest/download/01_sigpatches.rpx", "/vol/external01/wiiu/environments/aroma/modules/setup/01_sigpatches.rpx", "romfs:/github-com.pem") == 1) {
            drawToScreen("Error while downloading Sigpatches");
            goto done;
        }

        drawToScreen("Downloading Homebrew App Store...");

        if(downloadFile("http://wiiubru.com/appstore/zips/appstore.zip", "/vol/external01/appstore.zip", "romfs:/wiiubru-com.pem") == 1) {
            drawToScreen("Error while downloading Homebrew App Store");
            goto done;
        }
        
        drawToScreen("Extracting Homebrew App Store...");

        extract_package("/vol/external01/appstore.zip");

        drawToScreen("Downloading SaveMii Mod WUT Port...");

        if(downloadFile("https://wiiu.cdn.fortheusers.org/zips/SaveMiiModWUTPort-wuhb.zip", "/vol/external01/savemii.zip", "romfs:/wiiubru-com.pem") == 1) {
            drawToScreen("Error while downloading SaveMii Mod WUT Port");
            goto done;
        }
        
        drawToScreen("Extracting SaveMii Mod WUT Port...");

        extract_package("/vol/external01/savemii.zip");

        drawToScreen("Cleaning files...");

        remove("/vol/external01/payloads.zip");
        remove("/vol/external01/base.zip");
        remove("/vol/external01/plugins.zip");
        remove("/vol/external01/appstore.zip");
        remove("/vol/external01/savemii.zip");
    }

done: ;
    WHBLogPrint("");
    drawToScreen("Done, press HOME to exit");

    // Wait until the user exits the application
    while (AppRunning()) { }

    romfsExit();
    shutdownState();
    ProcUIShutdown();

    revertMainHook();

    return 0;
}
