// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/DelegateCombinations.h"

class FRHICommandListImmediate;

/** 
 * Interface for registering render capture plugins and capturing with them.
 */
namespace RenderCaptureInterface
{
	/** Call from rendering thread code to begin a capture. */
	RENDERCORE_API void BeginCapture(FRHICommandListImmediate* RHICommandList, TCHAR const* Name = TEXT(""));
	/** Call from rendering thread code to end a block. */
	RENDERCORE_API void EndCapture(FRHICommandListImmediate* RHICommandList);

	/** Helper for capturing within a scope. */
	class FScopedCapture
	{
	public:
		/** Use this constructor if not on rendering thread. Use bEnable to allow control over the capture frequency. */
		RENDERCORE_API FScopedCapture(bool bEnable, TCHAR const* Name = TEXT(""));
		/** Use this constructor if on rendering thread. Use bEnable to allow control over the capture frequency. */
		RENDERCORE_API FScopedCapture(bool bEnable, FRHICommandListImmediate* RHICommandList, TCHAR const* Name = TEXT(""));
		
		RENDERCORE_API ~FScopedCapture();
	
	private:
		bool bCapture;
		FRHICommandListImmediate* RHICmdList;
	};

	/**
	 * Registration code.
	 * Any capture plugins should register callbacks with this API.
	 */ 
	DECLARE_DELEGATE_TwoParams(FOnBeginCaptureDelegate, FRHICommandListImmediate*, TCHAR const*);
	DECLARE_DELEGATE_OneParam(FOnEndCaptureDelegate, FRHICommandListImmediate*);

	/** Register capture Begin and End delegates. */
	RENDERCORE_API void RegisterCallbacks(FOnBeginCaptureDelegate BeginDelegate, FOnEndCaptureDelegate EndDelegate);
	/** Unregister capture delegates. */
	RENDERCORE_API void UnregisterCallbacks();
}
