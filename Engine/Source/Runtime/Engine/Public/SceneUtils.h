// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/**
 *
 * This file contains the various draw mesh macros that display draw calls
 * inside of PIX.
 */

// Colors that are defined for a particular mesh type
// Each event type will be displayed using the defined color
#pragma once

#include "ProfilingDebugging/RealtimeGPUProfiler.h"

enum class EShadingPath
{
	Mobile,
	Deferred,
	Num,
};

enum class EMobileHDRMode
{
	Unset,
	Disabled,
	EnabledFloat16,
	EnabledMosaic,
	EnabledRGBE,
	EnabledRGBA8
};

/** True if HDR is enabled for the mobile renderer. */
ENGINE_API bool IsMobileHDR();

/** True if the mobile renderer is emulating HDR in a 32bpp render target. */
ENGINE_API bool IsMobileHDR32bpp();

/** True if the mobile renderer is emulating HDR with mosaic. */
ENGINE_API bool IsMobileHDRMosaic();

ENGINE_API EMobileHDRMode GetMobileHDRMode();

ENGINE_API bool IsMobileColorsRGB();

// Callback for calling one action (typical use case: delay a clear until it's actually needed)
class FDelayedRendererAction
{
public:
	typedef void (TDelayedFunction)(FRHICommandListImmediate& RHICommandList, void* UserData);

	FDelayedRendererAction()
		: Function(nullptr)
		, UserData(nullptr)
		, bFunctionCalled(false)
	{
	}

	FDelayedRendererAction(TDelayedFunction* InFunction, void* InUserData)
		: Function(InFunction)
		, UserData(InUserData)
		, bFunctionCalled(false)
	{
	}

	inline void SetDelayedFunction(TDelayedFunction* InFunction, void* InUserData)
	{
		check(!bFunctionCalled);
		check(!Function);
		Function = InFunction;
		UserData = InUserData;
	}

	inline bool HasDelayedFunction() const
	{
		return Function != nullptr;
	}

	inline void RunFunctionOnce(FRHICommandListImmediate& RHICommandList)
	{
		if (!bFunctionCalled)
		{
			if (Function)
			{
				Function(RHICommandList, UserData);
			}
			bFunctionCalled = true;
		}
	}

	inline bool HasBeenCalled() const
	{
		return bFunctionCalled;
	}

protected:
	TDelayedFunction* Function;
	void* UserData;
	bool bFunctionCalled;
};
