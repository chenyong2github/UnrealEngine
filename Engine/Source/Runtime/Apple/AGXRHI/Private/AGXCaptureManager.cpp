// Copyright Epic Games, Inc. All Rights Reserved.

#include "AGXRHIPrivate.h"
#include "AGXCaptureManager.h"
#include "AGXCommandQueue.h"
#include "capture_manager.hpp"

bool GAGXSupportsCaptureManager = false;

FAGXCaptureManager::FAGXCaptureManager(FAGXCommandQueue& InQueue)
: Queue(InQueue)
{
	MTLPP_IF_AVAILABLE(10.13, 11.0, 11.0)
	{
		GAGXSupportsCaptureManager = true;
		
		mtlpp::CaptureManager Manager = mtlpp::CaptureManager::SharedCaptureManager();
		Manager.SetDefaultCaptureScope(Manager.NewCaptureScopeWithDevice(GMtlppDevice));
		Manager.GetDefaultCaptureScope().SetLabel(@"1 Frame");
		
		FAGXCaptureScope DefaultScope;
		DefaultScope.MTLScope = Manager.GetDefaultCaptureScope();
		DefaultScope.Type = EAGXCaptureTypePresent;
		DefaultScope.StepCount = 1;
		DefaultScope.LastTrigger = 0;
		ActiveScopes.Add(DefaultScope);
		DefaultScope.MTLScope.BeginScope();
		
		uint32 PresentStepCounts[] = {2, 5, 10, 15, 30, 60, 90, 120};
		for (uint32 i = 0; i < (sizeof(PresentStepCounts) / sizeof(uint32)); i++)
		{
			FAGXCaptureScope Scope;
			Scope.MTLScope = Manager.NewCaptureScopeWithDevice(GMtlppDevice);
			Scope.MTLScope.SetLabel(FString::Printf(TEXT("%u Frames"), PresentStepCounts[i]).GetNSString());
			Scope.Type = EAGXCaptureTypePresent;
			Scope.StepCount = PresentStepCounts[i];
			Scope.LastTrigger = 0;
			ActiveScopes.Add(Scope);
			Scope.MTLScope.BeginScope();
		}
	}
}

FAGXCaptureManager::~FAGXCaptureManager()
{
}

void FAGXCaptureManager::PresentFrame(uint32 FrameNumber)
{
	if (GAGXSupportsCaptureManager)
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
				Scope.MTLScope.EndScope();
				Scope.MTLScope.BeginScope();
				Scope.LastTrigger = FrameNumber;
			}
		}
	}
	else
	{
		Queue.InsertDebugCaptureBoundary();
	}
}

void FAGXCaptureManager::BeginCapture(void)
{
	if (GAGXSupportsCaptureManager)
	{
		mtlpp::CaptureManager::SharedCaptureManager().StartCaptureWithDevice(GMtlppDevice);
	}
}

void FAGXCaptureManager::EndCapture(void)
{
	if (GAGXSupportsCaptureManager)
	{
		mtlpp::CaptureManager::SharedCaptureManager().StopCapture();
	}
}

