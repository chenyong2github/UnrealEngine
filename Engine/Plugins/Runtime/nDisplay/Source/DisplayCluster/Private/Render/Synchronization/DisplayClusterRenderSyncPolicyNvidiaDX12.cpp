// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNvidiaDX12.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"

// The following #undef is a temporary hacky fix to avoid GetRenderTargetFormat redefinition
// error. Both D3D11Viewport.h and D3D12Viewport.h public headers have the same function that
// leads to build error in Unity builds. 
#define GetRenderTargetFormat GetRenderTargetFormat_D3D12
#include "D3D12RHI/Private/D3D12RHIPrivate.h"
#include "D3D12Viewport.h"
#undef GetRenderTargetFormat

void D3D12RHI_API Temporary_WaitForFrameEventCompletion(FD3D12Viewport& D3D12Viewport);
void D3D12RHI_API Temporary_IssueFrameEvent(FD3D12Viewport& D3D12Viewport);


void FDisplayClusterRenderSyncPolicyNvidiaDX12::WaitForFrameCompletion()
{
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		FD3D12Viewport* const D3D12Viewport = static_cast<FD3D12Viewport*>(GEngine->GameViewport->Viewport->GetViewportRHI().GetReference());
		if (D3D12Viewport)
		{
			Temporary_IssueFrameEvent(*D3D12Viewport);
			Temporary_WaitForFrameEventCompletion(*D3D12Viewport);
		}
	}
}
