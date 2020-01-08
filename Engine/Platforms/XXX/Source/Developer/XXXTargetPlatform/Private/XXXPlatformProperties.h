// Copyright Epic Games, Inc. All Rights Reserved.

/*================================================================================
	XXXProperties.h - Basic static properties of a platform 
	These are shared between:
		the runtime platform - via FPlatformProperties
		the target platforms - via ITargetPlatform
==================================================================================*/

#pragma once

#include "GenericPlatform/GenericPlatformProperties.h"


/**
 * Implements XXX platform properties.
 */
struct FXXXPlatformProperties : public FGenericPlatformProperties
{
	static FORCEINLINE const char* GetPhysicsFormat( )
	{
		return "PhysXPC";
	}

	static FORCEINLINE bool HasEditorOnlyData( )
	{
		return false;
	}

	static FORCEINLINE const char* PlatformName()
	{
		return "XXX";
	}

	static FORCEINLINE const char* IniPlatformName()
	{
		return "XXX";
	}

	static FORCEINLINE bool IsGameOnly()
	{
		return true;
	}

	static FORCEINLINE bool IsClientOnly()
	{
		return !WITH_SERVER_CODE;
	}

	static FORCEINLINE bool RequiresCookedData()
	{
		return true;
	}

	static FORCEINLINE bool HasSecurePackageFormat()
	{
		return true;
	}

	static FORCEINLINE bool SupportsLowQualityLightmaps()
	{
		return false;
	}

	static FORCEINLINE bool SupportsBuildTarget( EBuildTargetType TargetType )
	{
		return (TargetType == EBuildTargetType::Game);
	}

	static FORCEINLINE bool SupportsAutoSDK()
	{
		return true;
	}

	static FORCEINLINE bool HasFixedResolution()
	{
		return true;
	}

	static FORCEINLINE bool AllowsFramerateSmoothing()
	{
		return true;
	}

	static FORCEINLINE bool AllowsCallStackDumpDuringAssert()
	{
		return true;
	}

	static FORCEINLINE bool SupportsAudioStreaming()
	{
		return !IsServerOnly();
	}
};
