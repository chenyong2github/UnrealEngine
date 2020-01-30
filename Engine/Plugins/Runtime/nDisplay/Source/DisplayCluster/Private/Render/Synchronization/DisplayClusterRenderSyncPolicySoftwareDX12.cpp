// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicySoftwareDX12.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"

// The following #undef is a temporary hacky fix to avoid GetRenderTargetFormat redefinition
// error. Both D3D11Viewport.h and D3D12Viewport.h public headers have the same function that
// leads to build error in Unity builds. 
#define GetRenderTargetFormat GetRenderTargetFormat_D3D12
#include "D3D12RHI/Private/D3D12RHIPrivate.h"
#include "D3D12Viewport.h"
#undef GetRenderTargetFormat


FDisplayClusterRenderSyncPolicySoftwareDX12::FDisplayClusterRenderSyncPolicySoftwareDX12()
{
}

FDisplayClusterRenderSyncPolicySoftwareDX12::~FDisplayClusterRenderSyncPolicySoftwareDX12()
{
}

bool FDisplayClusterRenderSyncPolicySoftwareDX12::SynchronizeClusterRendering(int32& InOutSyncInterval)
{
	if(!(GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport))
	{
		return false;
	}

	FD3D12Viewport* const Viewport = static_cast<FD3D12Viewport*>(GEngine->GameViewport->Viewport->GetViewportRHI().GetReference());
	check(Viewport);

#if !WITH_EDITOR
	// Issue frame event
	Viewport->IssueFrameEvent();
	// Wait until GPU finish last frame commands
	Viewport->WaitForFrameEventCompletion();
#endif

	//@todo DWM based synchronization

	// As a temporary solution, synchronize render threads on a barrier only
	SyncBarrierRenderThread();
	// Tell a caller that he still needs to present a frame
	return true;
}
