// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Viewport/Containers/DisplayClusterViewportProxy_ExchangeContainer.h"

#include "Render/Viewport/Containers/DisplayClusterViewport_Context.h"
#include "Render/Viewport/RenderTarget/DisplayClusterRenderTargetResource.h"
#include "Render/Viewport/DisplayClusterViewport.h"
#include "Render/Viewport/DisplayClusterViewportProxy.h"


FDisplayClusterViewportProxy_ExchangeContainer::FDisplayClusterViewportProxy_ExchangeContainer(const FDisplayClusterViewport* SrcViewport)
{
	if (SrcViewport)
	{
		RenderSettings = SrcViewport->RenderSettings;

		RenderSettingsICVFX.SetParameters(SrcViewport->RenderSettingsICVFX);
		PostRenderSettings.SetParameters(SrcViewport->PostRenderSettings);

		ProjectionPolicy = SrcViewport->ProjectionPolicy;
		Contexts         = SrcViewport->Contexts;

		// Save resources ptrs into container
		RenderTargets    = SrcViewport->RenderTargets;

		OutputFrameTargetableResources     = SrcViewport->OutputFrameTargetableResources;
		AdditionalFrameTargetableResources = SrcViewport->AdditionalFrameTargetableResources;

		InputShaderResources = SrcViewport->InputShaderResources;
		AdditionalTargetableResources = SrcViewport->AdditionalTargetableResources;
		MipsShaderResources = SrcViewport->MipsShaderResources;
	}
}

void FDisplayClusterViewportProxy_ExchangeContainer::CopyTo(FDisplayClusterViewportProxy* DstViewportProxy) const
{
	if (DstViewportProxy)
	{
		DstViewportProxy->RenderSettings = RenderSettings;

		DstViewportProxy->RenderSettingsICVFX.SetParameters(RenderSettingsICVFX);
		DstViewportProxy->PostRenderSettings.SetParameters(PostRenderSettings);

		DstViewportProxy->ProjectionPolicy = ProjectionPolicy;
		DstViewportProxy->Contexts         = Contexts;

		// Update viewport proxy resources from container
		DstViewportProxy->RenderTargets    = RenderTargets;

		DstViewportProxy->OutputFrameTargetableResources = OutputFrameTargetableResources;
		DstViewportProxy->AdditionalFrameTargetableResources = AdditionalFrameTargetableResources;

		DstViewportProxy->InputShaderResources = InputShaderResources;
		DstViewportProxy->AdditionalTargetableResources = AdditionalTargetableResources;
		DstViewportProxy->MipsShaderResources = MipsShaderResources;
	}
}
