// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <Metal/Metal.h>
#include "capture_scope.hpp"
#include "device.hpp"

class FAGXCommandQueue;

class FAGXCaptureManager
{
public:
	FAGXCaptureManager(FAGXCommandQueue& Queue);
	~FAGXCaptureManager();
	
	// Called by the AGXRHI code to trigger the provided capture scopes visible in Xcode.
	void PresentFrame(uint32 FrameNumber);
	
	// Programmatic captures without an Xcode capture scope.
	// Use them to instrument the code manually to debug issues.
	void BeginCapture(void);
	void EndCapture(void);
	
private:
	FAGXCommandQueue& Queue;
	bool bSupportsCaptureManager;
	
private:
	enum EAGXCaptureType
	{
		EAGXCaptureTypeUnknown,
		EAGXCaptureTypeFrame, // (BeginFrame-EndFrame) * StepCount
		EAGXCaptureTypePresent, // (Present-Present) * StepCount
		EAGXCaptureTypeViewport, // (Present-Present) * Viewports * StepCount
	};

	struct FAGXCaptureScope
	{
		EAGXCaptureType Type;
		uint32 StepCount;
		uint32 LastTrigger;
		mtlpp::CaptureScope MTLScope;
	};
	
	TArray<FAGXCaptureScope> ActiveScopes;
};
