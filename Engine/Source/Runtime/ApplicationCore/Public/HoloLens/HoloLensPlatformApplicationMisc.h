// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenericPlatform/GenericPlatformApplicationMisc.h"

struct APPLICATIONCORE_API FHoloLensPlatformApplicationMisc : public FGenericPlatformApplicationMisc
{
	static class FOutputDeviceError* GetErrorOutputDevice();
	static class GenericApplication* CreateApplication();
	static void PumpMessages(bool bFromMainLoop);
	static float GetDPIScaleFactorAtPoint(float X, float Y);
	static void ClipboardCopy(const TCHAR* Str);
	static void ClipboardPaste(class FString& Dest);
};

typedef FHoloLensPlatformApplicationMisc FPlatformApplicationMisc;
