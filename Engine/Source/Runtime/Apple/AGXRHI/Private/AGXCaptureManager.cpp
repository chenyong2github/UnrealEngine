// Copyright Epic Games, Inc. All Rights Reserved.

#include "AGXRHIPrivate.h"
#include "AGXCaptureManager.h"
#include "AGXCommandQueue.h"

FAGXCaptureManager::FAGXCaptureManager(FAGXCommandQueue& InQueue)
: Queue(InQueue)
{
	MTLCaptureManager* CaptureManager = [MTLCaptureManager sharedCaptureManager];
	
	id<MTLCaptureScope> DefaultCaptureScope = [CaptureManager newCaptureScopeWithDevice:GMtlDevice];
	DefaultCaptureScope.label = @"1 Frame";
	
	CaptureManager.defaultCaptureScope = DefaultCaptureScope;
	
	FAGXCaptureScope DefaultScope;
	DefaultScope.CaptureScope = MakeUnique<RetainedCaptureScopeType>(DefaultCaptureScope);
	DefaultScope.Type = EAGXCaptureTypePresent;
	DefaultScope.StepCount = 1;
	DefaultScope.LastTrigger = 0;
	ActiveScopes.Add(DefaultScope);
	
	[DefaultCaptureScope beginScope];
	
	// The local reference is no longer required (object lifetime is managed by DefaultScope).
	[DefaultCaptureScope release];
	
	uint32 PresentStepCounts[] = {2, 5, 10, 15, 30, 60, 90, 120};
	for (uint32 i = 0; i < (sizeof(PresentStepCounts) / sizeof(uint32)); i++)
	{
		id<MTLCaptureScope> CaptureScope = [CaptureManager newCaptureScopeWithDevice:GMtlDevice];
		CaptureScope.label = [NSString stringWithFormat:@"%u Frames", PresentStepCounts[i]];
	
		FAGXCaptureScope Scope;
		Scope.CaptureScope = MakeUnique<RetainedCaptureScopeType>(CaptureScope);
		Scope.Type = EAGXCaptureTypePresent;
		Scope.StepCount = PresentStepCounts[i];
		Scope.LastTrigger = 0;
		ActiveScopes.Add(Scope);
	
		[CaptureScope beginScope];
	
		// The local reference is no longer required (object lifetime is managed by Scope).
		[CaptureScope release];
	}
}

FAGXCaptureManager::~FAGXCaptureManager()
{
}

void FAGXCaptureManager::PresentFrame(uint32 FrameNumber)
{
	for (FAGXCaptureScope& Scope : ActiveScopes)
	{
		uint32 Diff = 0;
		if (FrameNumber > Scope.LastTrigger)
		{
			Diff = FrameNumber - Scope.LastTrigger;
		}
		else
		{
			Diff = (UINT32_MAX - Scope.LastTrigger) + FrameNumber;
		}
		
		if (Diff >= Scope.StepCount)
		{
			[Scope.CaptureScope.Get()->Object endScope];
			[Scope.CaptureScope.Get()->Object beginScope];
			Scope.LastTrigger = FrameNumber;
		}
	}
}

void FAGXCaptureManager::BeginCapture()
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS

	[[MTLCaptureManager sharedCaptureManager] startCaptureWithDevice:GMtlDevice];

PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void FAGXCaptureManager::EndCapture()
{
	[[MTLCaptureManager sharedCaptureManager] stopCapture];
}
