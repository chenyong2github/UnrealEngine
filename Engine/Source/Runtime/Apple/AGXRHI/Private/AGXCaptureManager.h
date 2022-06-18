// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class FAGXCaptureManager
{
public:
	FAGXCaptureManager(FAGXCommandQueue& Queue);
	~FAGXCaptureManager();
	
	// Called by the AGXRHI code to trigger the provided capture scopes visible in Xcode.
	void PresentFrame(uint32 FrameNumber);
	
	// Programmatic captures without an Xcode capture scope.
	// Use them to instrument the code manually to debug issues.
	void BeginCapture();
	void EndCapture();
	
private:
	FAGXCommandQueue& Queue;
	
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
		TRefCountPtr<FMTLCaptureScope> CaptureScope;

		FAGXCaptureScope()
		: Type(EAGXCaptureTypeUnknown)
		, StepCount(0)
		, LastTrigger(0)
		, CaptureScope()
		{
		}

		FAGXCaptureScope(const FAGXCaptureScope& Other)
		: Type(Other.Type)
		, StepCount(Other.StepCount)
		, LastTrigger(Other.LastTrigger)
		, CaptureScope(Other.CaptureScope)
		{
		}

		FAGXCaptureScope(FAGXCaptureScope&& Other)
		: Type(Other.Type)
		, StepCount(Other.StepCount)
		, LastTrigger(Other.LastTrigger)
		, CaptureScope(MoveTemp(Other.CaptureScope))
		{
		}
	};
	
	TArray<FAGXCaptureScope> ActiveScopes;
};
