// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Synchronization/DisplayClusterRenderSyncPolicyNvidiaDX11.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"

#include "ShaderCore.h"
#include "D3D11RHI/Private/Windows/D3D11RHIBasePrivate.h"
#include "D3D11State.h"
#include "D3D11Resources.h"
#include "D3D11Viewport.h"


void FDisplayClusterRenderSyncPolicyNvidiaDX11::WaitForFrameCompletion()
{
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		FD3D11Viewport* const D3D11Viewport = static_cast<FD3D11Viewport*>(GEngine->GameViewport->Viewport->GetViewportRHI().GetReference());
		if (D3D11Viewport)
		{
			D3D11Viewport->IssueFrameEvent();
			D3D11Viewport->WaitForFrameEventCompletion();
		}
	}
}
