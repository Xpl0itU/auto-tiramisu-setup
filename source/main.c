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

#include "state.h"

#include "kernel.h"
#include "zip/zip.h"

#define ARRAY_LENGTH(array) (sizeof((array)) / sizeof((array)[0]))
#define IO_BUFSIZE	(128 * 1024) // 128 KB

const char *skip_file_list[] =
{
	"manifest.install",
	"info.json",
    "versions.json",
    "screen1.png",
    "screen2.png"
};

void __init_wut_malloc();

// Initialize correct heaps for CustomRPXLoader
void __preinit_user(MEMHeapHandle *outMem1, MEMHeapHandle *outFG, MEMHeapHandle *outMem2) {
    __init_wut_malloc();
}

static int initSocket(void *ptr, curl_socket_t socket, curlsocktype type)
{
	int o = 1;

	// Activate WinScale
	int r = setsockopt(socket, SOL_SOCKET, SO_WINSCALE, &o, sizeof(o));
	if(r != 0)
	{
		WHBLogPrintf("initSocket: Error setting WinScale: %d", r);
		return CURL_SOCKOPT_ERROR;
	}

	//Activate TCP SAck
	r = setsockopt(socket, SOL_SOCKET, SO_TCPSACK, &o, sizeof(o));
	if(r != 0)
	{
		WHBLogPrintf("initSocket: Error setting TCP SAck: %d", r);
		return CURL_SOCKOPT_ERROR;
	}

	// Disable slowstart. Should be more important fo a server but doesn't hurt a client, too
	r = setsockopt(socket, SOL_SOCKET, 0x4000, &o, sizeof(o));
	if(r != 0)
	{
		WHBLogPrintf("initSocket: Error setting Noslowstart: %d", r);
		return CURL_SOCKOPT_ERROR;
	}

	o = 0;
	// Disable TCP keepalive - libCURL default
	r = setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, &o, sizeof(o));
	if(r != 0)
	{
		WHBLogPrintf("initSocket: Error setting TCP nodelay: %d", r);
		return CURL_SOCKOPT_ERROR;
	}

	o = IO_BUFSIZE;
	// Set receive buffersize
	r = setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &o, sizeof(o));
	if(r != 0)
	{
		WHBLogPrintf("initSocket: Error setting RBS: %d", r);
		return CURL_SOCKOPT_ERROR;
	}

	return CURL_SOCKOPT_OK;
}

static size_t writefunction(void *ptr, size_t size, size_t nmemb, void *stream)
{
  size_t written = fwrite(ptr, size, nmemb, (FILE *)stream);
  return written;
}

int make_file_path(const char* name)
{
	int err = 0;
	char *_name = strdup(name), *p;
	for (p = strchr(_name+1, '/'); p; p = strchr(p+1, '/'))
	{
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

void extract_package(char *path)
{
	struct zip_t *zip = zip_open(path, 0, 'r');
	for (int i = 0; i < zip_total_entries(zip); i++)
	{
		zip_entry_openbyindex(zip, i);
		
		// get file name
		const char *name = zip_entry_name(zip);
	
		// check if the file should be skipped
		int skip = 0;
		for (int j = 0; j < ARRAY_LENGTH(skip_file_list); j++)
			skip |= strcmp(name, skip_file_list[j]) == 0;
		
		// extract the file
		if (!skip && make_file_path(name))
			zip_entry_fread(zip, name);
		
		zip_entry_close(zip);
	}
	zip_close(zip);
}

int downloadFile(const char* url, const char* path, const char* cert) {
    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK) {
        WHBLogPrintf("curl_global_init: %d", res);
        return 1;
    }

    // Start a curl session
    CURL* curl = curl_easy_init();
    if (!curl) {
        WHBLogPrintf("curl_easy_init: failed");
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

    WHBLogPrintf("Starting download...");
    WHBLogConsoleDraw();

    // Perform the download
    FILE *file = fopen(path, "wb");
    if(file) {
        /* write the page body to this file handle */
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);

        WHBLogPrintf("Writing...");
        WHBLogConsoleDraw();
        /* get it! */
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            WHBLogPrintf("curl_easy_perform: %d", res);
            return 1;
        }

        /* close the header file */
        fclose(file);
    }

    // Done, clean up and exit
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return 0;
}

int main()
{
    // Initialize ProcUI
    WHBProcInit();
    initState();

    // Initialize a log console
    WHBLogConsoleInit();

    // Initialize romfs for the certificate bundle
    romfsInit();

    WHBLogPrintf("Automatic Wii U Homebrew Setup");
    WHBLogPrintf("");
    WHBLogConsoleDraw();

    WHBLogPrintf("Downloading Tiramisu...");
    WHBLogConsoleDraw();

    if(downloadFile("https://github.com/wiiu-env/Tiramisu/releases/download/v0.1/environmentloader-7194938+wiiu-nanddumper-payload-5c5ec09+fw_img_loader-c2da326+payloadloaderinstaller-98367a9+tiramisu-7b881d3.zip", "/vol/external01/tiramisu.zip", "romfs:/github-com.pem") == 1) {
        WHBLogPrintf("Error while downloading Tiramisu");
        WHBLogConsoleDraw();
        goto done;
    }
    
    WHBLogPrintf("Extracting Tiramisu...");
    WHBLogConsoleDraw();

    extract_package("/vol/external01/tiramisu.zip");

    WHBLogPrintf("Downloading Sigpatches...");
    WHBLogConsoleDraw();

    if(downloadFile("https://github.com/marco-calautti/SigpatchesModuleWiiU/releases/latest/download/01_sigpatches.rpx", "/vol/external01/wiiu/environments/tiramisu/modules/setup/01_sigpatches.rpx", "romfs:/github-com.pem") == 1) {
        WHBLogPrintf("Error while downloading Sigpatches");
        WHBLogConsoleDraw();
        goto done;
    }

    WHBLogPrintf("Downloading Homebrew App Store...");
    WHBLogConsoleDraw();

    if(downloadFile("http://wiiubru.com/appstore/zips/appstore.zip", "/vol/external01/appstore.zip", "romfs:/wiiubru-com.pem") == 1) {
        WHBLogPrintf("Error while downloading Homebrew App Store");
        WHBLogConsoleDraw();
        goto done;
    }
    
    WHBLogPrintf("Extracting Homebrew App Store...");
    WHBLogConsoleDraw();

    extract_package("/vol/external01/appstore.zip");

    WHBLogPrintf("Downloading SaveMii Mod WUT Port...");
    WHBLogConsoleDraw();

    if(downloadFile("https://wiiubru.com/appstore/zips/SaveMiiModWUTPort.zip", "/vol/external01/savemii.zip", "romfs:/wiiubru-com.pem") == 1) {
        WHBLogPrintf("Error while downloading SaveMii Mod WUT Port");
        WHBLogConsoleDraw();
        goto done;
    }
    
    WHBLogPrintf("Extracting SaveMii Mod WUT Port...");
    WHBLogConsoleDraw();

    extract_package("/vol/external01/savemii.zip");

    WHBLogPrintf("Cleaning files...");
    WHBLogConsoleDraw();

    remove("/vol/external01/tiramisu.zip");
    remove("/vol/external01/appstore.zip");
    remove("/vol/external01/savemii.zip");

done: ;
    WHBLogPrintf("Done, press HOME to exit");
    WHBLogConsoleDraw();

    // Wait until the user exits the application
    while (AppRunning()) { }

    romfsExit();
    shutdownState();
    ProcUIShutdown();

    revertMainHook();

    return 0;
}
