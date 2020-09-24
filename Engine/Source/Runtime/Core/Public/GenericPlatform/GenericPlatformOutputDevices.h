// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/UnrealString.h"
#include "GenericPlatform/GenericPlatform.h"
#include "Templates/UniquePtr.h"

class FOutputDeviceConsole;
class FOutputDeviceError;
class FOutputDeviceFile;

/**
 * Generic implementation for most platforms
 */
struct CORE_API FGenericPlatformOutputDevices
{
	/** Add output devices which can vary depending on platform, configuration, command line parameters. */
	static void							SetupOutputDevices();
	static FString						GetAbsoluteLogFilename();
	static FOutputDevice*				GetLog();
	static void							GetPerChannelFileOverrides(TArray<FOutputDevice*>& OutputDevices);
	static FOutputDevice*				GetEventLog()
	{
		return nullptr; // normally only used for dedicated servers
	}

	static FOutputDeviceError*			GetError();
	static FFeedbackContext*            GetFeedbackContext();

protected:
	static void InitDefaultOutputDeviceFile();

	static const SIZE_T AbsoluteFileNameMaxLength = 1024;
	static TCHAR CachedAbsoluteFilename[AbsoluteFileNameMaxLength];
	static TUniquePtr<FOutputDeviceFile> DefaultOutputDeviceFileTempHolder;
};
