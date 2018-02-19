#include <string.h>
#include <malloc.h>
#include "dynamic_libs/os_functions.h"
#include "dynamic_libs/fs_functions.h"
#include "dynamic_libs/sys_functions.h"
#include "dynamic_libs/vpad_functions.h"
#include "system/memory.h"
//#include "dynamic_libs/socket_functions.h"
//#include "utils/logger.h"
#include "utils/utils.h"
#include "common/common.h"
#include "fs/sd_fat_devoptab.h"
#include <dirent.h>

#define TITLE_TEXT                  "WUP installer Macnolo 1.4"

#define HBL_TITLE_ID	0x0005000013374842

#define MCP_COMMAND_INSTALL_ASYNC   0x81
#define MAX_INSTALL_PATH_LENGTH     0x27F

static int doInstall = 0;
static int installCompleted = 0;
static int installSuccess = 0;
static int installToUsb = 0;
static u32 installError = 0;
static u64 installedTitle = 0;
static u64 baseTitleId = 0;
static int dirNum = 0;
static char installFolder[256] = "";
static char lastFolder[256] = "";
static char errorText1[128] = "";
static char errorText2[128] = "";
static bool folderSelect[1024] = {false};

static int IosInstallCallback(unsigned int errorCode, unsigned int * priv_data)
{
    installError = errorCode;
    installCompleted = 1;
    return 0;
}

static void GetInstallDir(char *dest, int size)
{
	DIR *dirPtr;
	struct dirent *dirEntry;
	dirPtr = opendir ("sd:/install/");
	int dirsFound = 0;

	if (dirPtr != NULL)
	{
		dirEntry = readdir(dirPtr);
		if (dirEntry != NULL && dirEntry->d_type == DT_DIR)
		{
			seekdir(dirPtr, dirNum);
			dirEntry = readdir(dirPtr);
			closedir (dirPtr);
			if (dirEntry != NULL && dirEntry->d_type == DT_DIR)
				__os_snprintf(dest, size, "install/%s", dirEntry->d_name);
			else
			{
				dirNum--;
				if (dirNum < 0)
					dirNum = 0;
			}
			dirsFound = 1;
		}
	}
	if (!dirsFound)
	{
		__os_snprintf(dest, size, "");
		dirNum = 0;
	}
}

static void setAllFolderSelect(bool state)
{
	DIR *dirPtr;
	struct dirent *dirEntry;

	for (int i = 0; i < 1023; i++)
		folderSelect[i] = false;

	if (state)
	{
		dirPtr = opendir ("sd:/install/");
		if (dirPtr != NULL)
		{
			int i = 0;
			while (1)
			{
				dirEntry = readdir(dirPtr);
				if (dirEntry == NULL || dirEntry->d_type != DT_DIR)
					break;
				folderSelect[i++] = true;
			}
			closedir (dirPtr);
		}
	}
}

static int getNextSelectedFolder(void)
{
	int i;
	for (i = 0; i < 1023; i++)
		if (folderSelect[i] == true)
			break;
	return (i < 1023) ? i : 0;
}

static int useFolderSelect(void)
{
	int i;
	int ret = 0;
	for (i = 0; i < 1023; i++)
		if (folderSelect[i] == true)
		{
			ret = 1;
			break;
		}
	return ret;
}

static void SetupInstallTitle(void)
{
	if (useFolderSelect())
		dirNum = getNextSelectedFolder();
	GetInstallDir(installFolder, sizeof(installFolder));
}

static void InstallTitle(void)
{
    errorText1[0] = 0;
    errorText2[0] = 0;
	installSuccess = 0;
	installedTitle = 0;
	installCompleted = 1;
	installError = 0;

	__os_snprintf(lastFolder, sizeof(lastFolder), installFolder);

	//!---------------------------------------------------
    //! This part of code originates from Crediars MCP patcher assembly code
    //! it is just translated to C
    //!---------------------------------------------------
    unsigned int mcpHandle = MCP_Open();
    if(mcpHandle == 0)
    {
        __os_snprintf(errorText1, sizeof(errorText1), "Fallo al abrir MCP.");
        return;
    }

    char text[256];
    unsigned int * mcpInstallInfo = (unsigned int *)OSAllocFromSystem(0x24, 0x40);
    char * mcpInstallPath = (char *)OSAllocFromSystem(MAX_INSTALL_PATH_LENGTH, 0x40);
    unsigned int * mcpPathInfoVector = (unsigned int *)OSAllocFromSystem(0x0C, 0x40);

    do
    {
		if(!mcpInstallInfo || !mcpInstallPath || !mcpPathInfoVector)
        {
            __os_snprintf(errorText1, sizeof(errorText1), "No se ha podido alojar memoria.");
            break;
        }

		__os_snprintf(text, sizeof(text), "/vol/app_sd/%s", installFolder);

        int result = MCP_InstallGetInfo(mcpHandle, text, mcpInstallInfo);
        if(result != 0)
        {
            __os_snprintf(errorText1, sizeof(errorText1), "MCP_InstallGetInfo 0x%08X", MCP_GetLastRawError());
            __os_snprintf(errorText2, sizeof(errorText2), "Confirma los archivos WUP que estan en la carpeta.");
            break;
        }

        u32 titleIdHigh = mcpInstallInfo[0];
        u32 titleIdLow = mcpInstallInfo[1];
		int spoofFiles = 0;
        if ((titleIdHigh == 00050010)
			&&    ((titleIdLow == 0x10041000)     // JAP Version.bin
				|| (titleIdLow == 0x10041100)     // USA Version.bin
				|| (titleIdLow == 0x10041200)))   // EUR Version.bin
		{
			spoofFiles = 1;
			installToUsb = 0;
		}

        if (spoofFiles
		   || (titleIdHigh == 0x0005000E)     // game update
           || (titleIdHigh == 0x00050000)     // game
           || (titleIdHigh == 0x0005000C)     // DLC
           || (titleIdHigh == 0x00050002))    // Demo
        {
            installedTitle = ((u64)titleIdHigh << 32ULL) | titleIdLow;

			result = MCP_InstallSetTargetDevice(mcpHandle, installToUsb);
			if(result != 0)
			{
				__os_snprintf(errorText1, sizeof(errorText1), "Error: MCP_InstallSetTargetDevice 0x%08X", MCP_GetLastRawError());
				if (installToUsb)
					__os_snprintf(errorText2, sizeof(errorText2), "Posible disco duro o usb desconectado o esta dañado");
				break;
			}
			result = MCP_InstallSetTargetUsb(mcpHandle, installToUsb);
			if(result != 0)
			{
				__os_snprintf(errorText1, sizeof(errorText1), "Error: MCP_InstallSetTargetUsb 0x%08X", MCP_GetLastRawError());
				if (installToUsb)
					__os_snprintf(errorText2, sizeof(errorText2), "Posible disco duro o usb desconectado o esta dañado");
				break;
			}

            mcpInstallInfo[2] = (unsigned int)MCP_COMMAND_INSTALL_ASYNC;
            mcpInstallInfo[3] = (unsigned int)mcpPathInfoVector;
            mcpInstallInfo[4] = (unsigned int)1;
            mcpInstallInfo[5] = (unsigned int)0;

            memset(mcpInstallPath, 0, MAX_INSTALL_PATH_LENGTH);
            __os_snprintf(mcpInstallPath, MAX_INSTALL_PATH_LENGTH, text);
            memset(mcpPathInfoVector, 0, 0x0C);

            mcpPathInfoVector[0] = (unsigned int)mcpInstallPath;
            mcpPathInfoVector[1] = (unsigned int)MAX_INSTALL_PATH_LENGTH;

			installCompleted = 0;
            result = IOS_IoctlvAsync(mcpHandle, MCP_COMMAND_INSTALL_ASYNC, 1, 0, mcpPathInfoVector, IosInstallCallback, mcpInstallInfo);
            if(result != 0)
            {
                __os_snprintf(errorText1, sizeof(errorText1), "Error: MCP_InstallTitleAsync 0x%08X", MCP_GetLastRawError());
                break;
            }

            while(!installCompleted)
            {
                memset(mcpInstallInfo, 0, 0x24);

                result = MCP_InstallGetProgress(mcpHandle, mcpInstallInfo);

                if(mcpInstallInfo[0] == 1)
                {
                    u64 installedSize, totalSize;
					totalSize = ((u64)mcpInstallInfo[3] << 32ULL) | mcpInstallInfo[4];
					installedSize = ((u64)mcpInstallInfo[5] << 32ULL) | mcpInstallInfo[6];
					int percent = (totalSize != 0) ? ((installedSize * 100.0f) / totalSize) : 0;
                    for(int i = 0; i < 2; i++)
                    {
                        OSScreenClearBufferEx(i, 0);

                        OSScreenPutFontEx(i, 0, 0, TITLE_TEXT);
						OSScreenPutFontEx(i, 0, 3, "Instalando titulo...");
                        OSScreenPutFontEx(i, 0, 4, installFolder);

                        __os_snprintf(text, sizeof(text), "%0.1f / %0.1f MB", titleIdHigh, titleIdLow, installedSize / (1024.0f * 1024.0f),
                                                                                                  totalSize / (1024.0f * 1024.0f), percent);
                        OSScreenPutFontEx(i, 0, 5, text);

                        if(percent == 100)
                        {
                            OSScreenPutFontEx(i, 0, 6, "Un momento...");
                        }
                        // Flip buffers
                        OSScreenFlipBuffersEx(i);
                    }
                }

                usleep(50000);
            }

            if(installError != 0)
            {
                if ((installError == 0xFFFCFFE9) && installToUsb)
				{
                    __os_snprintf(errorText1, sizeof(errorText1), "Error: 0x%08X acceso fallido", installError);
                }
                else
				{
                    __os_snprintf(errorText1, sizeof(errorText1), "Error: codigo de error de instalacion 0x%08X", installError);
					if (installError == 0xFFFBF446 || installError == 0xFFFBF43F)
						__os_snprintf(errorText2, sizeof(errorText2), "Posible mal o faltante archivo title.tik");
					else if (installError == 0xFFFBF441)
						__os_snprintf(errorText2, sizeof(errorText2), "Posible mal o faltante archivo title.tik para DLC");
					else if (installError == 0xFFFCFFE4)
						__os_snprintf(errorText2, sizeof(errorText2), "Falta memoria en la memoria destino");
					else if (installError == 0xFFFFF825)
						__os_snprintf(errorText2, sizeof(errorText2), "Posible mala memoria SD. Prueba formatear a 32k bloques o remplaza");
					else if ((installError & 0xFFFF0000) == 0xFFFB0000)
						__os_snprintf(errorText2, sizeof(errorText2), "DLCs o juegos de eShop necesitan Sigpatcher u otro CFW");
                }
            }
            else
            {
                installSuccess = 1;
            }
        }
        else
        {
            __os_snprintf(errorText1, sizeof(errorText1), "No es juego, actualizacion, dlc ni demo");
        }
    }
    while(0);

	folderSelect[dirNum] = false;
	if (installSuccess && useFolderSelect())
	{
		dirNum = getNextSelectedFolder();
		doInstall = 1;
	}
	else
		doInstall = 0;

	MCP_Close(mcpHandle);
    if(mcpPathInfoVector)
        OSFreeToSystem(mcpPathInfoVector);
    if(mcpInstallPath)
        OSFreeToSystem(mcpInstallPath);
    if(mcpInstallInfo)
        OSFreeToSystem(mcpInstallInfo);
}

/* Entry point */
int Menu_Main(void)
{
    //!*******************************************************************
    //!                   Initialize function pointers                   *
    //!*******************************************************************
    //! do OS (for acquire) and sockets first so we got logging
    InitOSFunctionPointers();
    InitFSFunctionPointers();
    InitSysFunctionPointers();
    InitVPadFunctionPointers();
    //InitSocketFunctionPointers();
	//log_init("192.168.0.100");

    //!*******************************************************************
    //!                    Initialize heap memory                        *
    //!*******************************************************************
    //! We don't need bucket and MEM1 memory so no need to initialize
    memoryInitialize();
    mount_sd_fat("sd");

    VPADInit();

    int update_screen = 1;
	int delay = 0;
    int vpadError = -1;
    VPADData vpad_data;

    // Prepare screen
    int screen_buf0_size = 0;
    int screen_buf1_size = 0;

    // Init screen and screen buffers
    OSScreenInit();
    screen_buf0_size = OSScreenGetBufferSizeEx(0);
    screen_buf1_size = OSScreenGetBufferSizeEx(1);

    unsigned char *screenBuffer = MEM1_alloc(screen_buf0_size + screen_buf1_size, 0x40);

    OSScreenSetBufferEx(0, screenBuffer);
    OSScreenSetBufferEx(1, (screenBuffer + screen_buf0_size));

    OSScreenEnableEx(0, 1);
    OSScreenEnableEx(1, 1);

    u64 currenTitleId = OSGetTitleID();
    int hblChannelLaunch = (currenTitleId == HBL_TITLE_ID);

    // in case we are not in mii maker but in system menu we start the installation
    if (currenTitleId != 0x000500101004A200 && // mii maker eur
        currenTitleId != 0x000500101004A100 && // mii maker usa
        currenTitleId != 0x000500101004A000 && // mii maker jpn
        !hblChannelLaunch)                     // HBL channel
    {
		InstallTitle();
        MEM1_free(screenBuffer);
        memoryRelease();
        SYSLaunchTitle(baseTitleId);

        return EXIT_RELAUNCH_ON_LOAD;
    }

	if (doInstall)
	{
		SetupInstallTitle();
		delay = 250;
	}

	baseTitleId = currenTitleId;

	while(1)
    {
		// print to TV and DRC
		if(update_screen)
		{
			GetInstallDir(installFolder, sizeof(installFolder));
			OSScreenClearBufferEx(0, 0);
			OSScreenClearBufferEx(1, 0);
			for(int i = 0; i < 2; i++)
			{
				char text[80];

				OSScreenPutFontEx(i, 0, 0, TITLE_TEXT);
				OSScreenPutFontEx(i, 0, 4, lastFolder);
				__os_snprintf(text, sizeof(text), "Instalacion del titulo %08X-%08X ", (u32)(installedTitle >> 32), (u32)(installedTitle & 0xffffffff));
				if( installSuccess)
				{
					__os_snprintf(text, sizeof(text), "%sterminado exitosamente.", text);
					OSScreenPutFontEx(i, 0, 3, text);
				}
				else if (installCompleted)
				{
					__os_snprintf(text, sizeof(text), "%s Fallido.", text);
					OSScreenPutFontEx(i, 0, 3, text);
					OSScreenPutFontEx(i, 0, 5, errorText1);
					OSScreenPutFontEx(i, 0, 6, errorText2);
				}

				if (!doInstall)
				{
					OSScreenPutFontEx(i, 0, 7, "Selecciona carpeta de instalacion:");
					__os_snprintf(text, sizeof(text), "%c  %s", folderSelect[dirNum] ? '*' : ' ', installFolder);
					OSScreenPutFontEx(i, 0, 8, text);

					OSScreenPutFontEx(i, 0, 9, "Presiona D-Pad U/D para cambiar carpeta.");
					OSScreenPutFontEx(i, 0, 10, "Presiona D-Pad L/R para seleccionar/deseleccionar carpeta.");
					OSScreenPutFontEx(0, 0, 11, "Presiona + para seleccionar todas las carpetas y - para deseleccionarlas todas las carpetas.");
          OSScreenPutFontEx(1, 0, 11, "Presiona +/- para seleccionar/deseleccionar todas las carpetas.");
					OSScreenPutFontEx(i, 0, 12, "Presiona A para instalar en la NAND.");
					OSScreenPutFontEx(i, 0, 13, "Presiona X para instalar en la memoria externa.");
					OSScreenPutFontEx(i, 0, 14, "Presiona Y para rescanear.");
				}
				else
				{
					OSScreenPutFontEx(i, 0, 8, installFolder);
					__os_snprintf(text, sizeof(text), "Se instalara en %d", delay / 50);
					OSScreenPutFontEx(i, 0, 9, text);
					OSScreenPutFontEx(i, 0, 10, "Presiona B para cancelar");
				}

				OSScreenPutFontEx(i, 0, 17, "Presiona HOME para regresar al HBL.");
			}

				// Flip buffers
				OSScreenFlipBuffersEx(0);
				OSScreenFlipBuffersEx(1);
		}
		update_screen = 0;

    VPADRead(0, &vpad_data, 1, &vpadError);
u32 pressedBtns = 0;
int yPressed = 0;

		if (!vpadError)
			pressedBtns = vpad_data.btns_d | vpad_data.btns_h;

		if (pressedBtns & VPAD_BUTTON_HOME)
		{
			doInstall = 0;
			break;
		}

		if (!(pressedBtns & VPAD_BUTTON_Y))
			yPressed = 0;

        if (!doInstall)
        {
			if (!(pressedBtns & (VPAD_BUTTON_UP | VPAD_BUTTON_DOWN)))
				delay = 0;

			if (pressedBtns & (VPAD_BUTTON_A | VPAD_BUTTON_X))	// install to NAND/USB
			{
				doInstall = 1;
				installToUsb = (pressedBtns & VPAD_BUTTON_X) ? 1: 0;
				SetupInstallTitle();
				if (hblChannelLaunch)
				{
					InstallTitle();
					update_screen = 1;
					if (doInstall)
						delay = 250;
				}
				else
					break;
			}
			else if (pressedBtns & VPAD_BUTTON_Y)		// remount SD
			{
				if (!yPressed)
				{
					unmount_sd_fat("sd");
					usleep(50000);
					mount_sd_fat("sd");
					setAllFolderSelect(false);
					dirNum = 0;
					update_screen = 1;
					usleep(50000);
				}
				yPressed = 1;
			}
			else if (pressedBtns & VPAD_BUTTON_UP)		// up directory
			{
				if (--delay <= 0)
				{
					if (dirNum < 1000)
						dirNum++;
					else
						dirNum = 0;
					delay = (vpad_data.btns_d & VPAD_BUTTON_UP) ? 6 : 0;
				}
			}
			else if (pressedBtns & VPAD_BUTTON_DOWN)	// down directory
			{
				if (--delay <= 0)
				{
					if (dirNum > 0)
						dirNum--;
					delay = (vpad_data.btns_d & VPAD_BUTTON_DOWN) ? 6 : 0;
				}
			}
			else if (pressedBtns & (VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT))	// unselect/select directory
			{
				folderSelect[dirNum] = (pressedBtns & VPAD_BUTTON_RIGHT) ? 1 : 0;
			}
			else if (pressedBtns & (VPAD_BUTTON_MINUS | VPAD_BUTTON_PLUS))	// unselect/select all directories
			{
				setAllFolderSelect((pressedBtns & VPAD_BUTTON_PLUS) ? true : false);
			}

			// folder selection button pressed ?
			update_screen |= (pressedBtns & (VPAD_BUTTON_UP | VPAD_BUTTON_DOWN | VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT
											| VPAD_BUTTON_PLUS | VPAD_BUTTON_MINUS | VPAD_BUTTON_Y)) ? 1 : 0;
		}
		else
		{
			if (pressedBtns & VPAD_BUTTON_B)	// cancel
			{
				doInstall = 0;
				installSuccess = 0;
				update_screen = 1;
				delay = 0;
			}
			else if (--delay <= 0)
			{
				if (hblChannelLaunch)
				{
					__os_snprintf(lastFolder, sizeof(lastFolder), installFolder);
					SetupInstallTitle();
					InstallTitle();
					update_screen = 1;
					if (doInstall)
						delay = 250;
				}
				else
					break;
			}
			else
				update_screen = (delay % 50 == 0) ? 1 : 0;
		}
		usleep(20000);
    }

	MEM1_free(screenBuffer);
	screenBuffer = NULL;

    //!*******************************************************************
    //!                    Enter main application                        *
    //!*******************************************************************
    unmount_sd_fat("sd");
    memoryRelease();

    if(doInstall)
    {
        SYSLaunchMenu();
        return EXIT_RELAUNCH_ON_LOAD;
    }

	if (!doInstall)
	{
		setAllFolderSelect(false);
		dirNum = 0;
		installFolder[0] = 0;
	}
	//log_deinit();
    return EXIT_SUCCESS;
}
