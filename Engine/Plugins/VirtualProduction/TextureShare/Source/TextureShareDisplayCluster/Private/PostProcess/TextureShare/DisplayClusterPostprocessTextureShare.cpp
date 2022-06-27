// Copyright Epic Games, Inc. All Rights Reserved.

#include "PostProcess/TextureShare/DisplayClusterPostprocessTextureShare.h"
#include "PostProcess/DisplayClusterPostprocessStrings.h"
#include "Misc/TextureShareDisplayClusterStrings.h"

#include "Module/TextureShareDisplayClusterLog.h"

#include "Containers/TextureShareCoreEnums.h"

#include "ITextureShare.h"
#include "ITextureShareAPI.h"
#include "ITextureShareObject.h"
#include "ITextureShareObjectProxy.h"
#include "ITextureShareDisplayCluster.h"

#include "Render/Viewport/IDisplayClusterViewportManager.h"

//////////////////////////////////////////////////////////////////////////////////////////////
namespace DisplayClusterPostProcessTextureShareHelpers
{
	static ITextureShareAPI& TextureShareAPI()
	{
		static ITextureShareAPI& TextureShareAPISingleton = ITextureShare::Get().GetTextureShareAPI();
		return TextureShareAPISingleton;
	}

	static FViewport* GetDisplayViewport(IDisplayClusterViewportManager* InViewportManager)
	{
		if (InViewportManager)
		{
			if (UWorld* CurrentWorld = InViewportManager->GetCurrentWorld())
			{
				if (UGameViewportClient* GameViewportClientPtr = CurrentWorld->GetGameViewport())
				{
					return GameViewportClientPtr->Viewport;
				}
			}
		}

		return nullptr;
	};
};
using namespace DisplayClusterPostProcessTextureShareHelpers;

//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterPostProcessTextureShare
//////////////////////////////////////////////////////////////////////////////////////////////
FDisplayClusterPostProcessTextureShare::FDisplayClusterPostProcessTextureShare(const FString& PostprocessId, const struct FDisplayClusterConfigurationPostprocess* InConfigurationPostprocess)
	: FDisplayClusterPostprocessBase(PostprocessId, InConfigurationPostprocess)
{
	if (TextureShareAPI().IsObjectExist(TextureShareDisplayClusterStrings::Default::ShareName))
	{
		UE_LOG(LogTextureShareDisplayClusterPostProcess, Error, TEXT("TextureShareDisplayCluster: Failed - Already exist"));
	}
	else
	{
		Object = TextureShareAPI().GetOrCreateObject(TextureShareDisplayClusterStrings::Default::ShareName);
		if (Object.IsValid())
		{
			ObjectProxy = Object->GetProxy();
		}

		if (IsActive())
		{
			// Initialize sync settings for nDisplay
			FTextureShareCoreSyncSettings SyncSetting;
			SyncSetting.FrameSyncSettings = Object->GetFrameSyncSettings(ETextureShareFrameSyncTemplate::DisplayCluster);

			Object->SetSyncSetting(SyncSetting);

			Object->BeginSession();

			UE_LOG(LogTextureShareDisplayClusterPostProcess, Log, TEXT("TextureShareDisplayCluster: Initialized"));
		}
		else
		{
			ReleaseDisplayClusterPostProcessTextureShare();

			UE_LOG(LogTextureShareDisplayClusterPostProcess, Error, TEXT("TextureShareDisplayCluster: Failed - initization failed"));
		}
	}
}

FDisplayClusterPostProcessTextureShare::~FDisplayClusterPostProcessTextureShare()
{
	if (Object.IsValid())
	{
		ReleaseDisplayClusterPostProcessTextureShare();
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////
const FString& FDisplayClusterPostProcessTextureShare::GetType() const
{
	static const FString Type(DisplayClusterPostprocessStrings::Postprocess::TextureShare);

	return Type;
}

void FDisplayClusterPostProcessTextureShare::ReleaseDisplayClusterPostProcessTextureShare()
{
	ObjectProxy.Reset();
	Object.Reset();

	TextureShareAPI().RemoveObject(TextureShareDisplayClusterStrings::Default::ShareName);
}

bool FDisplayClusterPostProcessTextureShare::HandleStartScene(IDisplayClusterViewportManager* InViewportManager)
{
	return true;
}

void FDisplayClusterPostProcessTextureShare::HandleEndScene(IDisplayClusterViewportManager* InViewportManager)
{
}

//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterPostProcessTextureShare::HandleSetupNewFrame(IDisplayClusterViewportManager* InViewportManager)
{
	if (IsActive() && Object->BeginFrameSync() && Object->IsFrameSyncActive())
	{
		// Update frame marker for current frame
		Object->GetCoreData().FrameMarker.NextFrame();

		// Share defined viewports from this DC node
		UpdateSupportedViews(InViewportManager);

		// Sync IPC data (read manual projection data)
		if (Object->FrameSync(ETextureShareSyncStep::FramePreSetupBegin) && Object->IsFrameSyncActive())
		{
			// Update TS manual projection policy on this node
			UpdateManualProjectionPolicy(InViewportManager);
		}
	}
}

void FDisplayClusterPostProcessTextureShare::HandleBeginNewFrame(IDisplayClusterViewportManager* InViewportManager, FDisplayClusterRenderFrame& InOutRenderFrame)
{
	if (IsActive() && Object->IsFrameSyncActive())
	{
		if (Object->FrameSync(ETextureShareSyncStep::FrameSetupBegin) && Object->IsFrameSyncActive())
		{
			// Register viewport mapping
			UpdateViews(InViewportManager);

			Object->EndFrameSync(GetDisplayViewport(InViewportManager));

			// Immediatelly begin proxy frame
			ENQUEUE_RENDER_COMMAND(DisplayClusterPostProcessTextureShare_UpdateObjectProxy)(
				[ObjectProxyRef = ObjectProxy.ToSharedRef()](FRHICommandListImmediate& RHICmdList)
				{
					ObjectProxyRef->BeginFrameSync_RenderThread(RHICmdList);
				});
		}
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterPostProcessTextureShare::HandleRenderFrameSetup_RenderThread(FRHICommandListImmediate& RHICmdList, const IDisplayClusterViewportManagerProxy* InViewportManagerProxy)
{
	check(IsInRenderingThread());

	if (IsActive() && ObjectProxy->IsFrameSyncActive_RenderThread())
	{
		// Share RTT with remote process
		ShareViewport_RenderThread(RHICmdList, InViewportManagerProxy, ETextureShareSyncStep::FrameProxyPreRenderEnd, EDisplayClusterViewportResourceType::InternalRenderTargetResource, TextureShareDisplayClusterStrings::Viewport::FinalColor);

		ObjectProxy->FrameSync_RenderThread(RHICmdList, ETextureShareSyncStep::FrameProxyPreRenderEnd);
	}
}

void FDisplayClusterPostProcessTextureShare::HandleBeginUpdateFrameResources_RenderThread(FRHICommandListImmediate& RHICmdList, const IDisplayClusterViewportManagerProxy* InViewportManagerProxy)
{
	check(IsInRenderingThread());

	if (IsActive() && ObjectProxy->IsFrameSyncActive_RenderThread())
	{
		// Share RTT with remote process
		ShareViewport_RenderThread(RHICmdList, InViewportManagerProxy, ETextureShareSyncStep::FrameProxyRenderEnd, EDisplayClusterViewportResourceType::InputShaderResource, TextureShareDisplayClusterStrings::Viewport::Input);
		ShareViewport_RenderThread(RHICmdList, InViewportManagerProxy, ETextureShareSyncStep::FrameProxyRenderEnd, EDisplayClusterViewportResourceType::MipsShaderResource, TextureShareDisplayClusterStrings::Viewport::Mips);

		ObjectProxy->FrameSync_RenderThread(RHICmdList, ETextureShareSyncStep::FrameProxyRenderEnd);
	}
}

void FDisplayClusterPostProcessTextureShare::HandleUpdateFrameResourcesAfterWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const IDisplayClusterViewportManagerProxy* InViewportManagerProxy)
{
	check(IsInRenderingThread());

	if (IsActive() && ObjectProxy->IsFrameSyncActive_RenderThread())
	{
		// Share RTT with remote process
		ShareViewport_RenderThread(RHICmdList, InViewportManagerProxy, ETextureShareSyncStep::FrameProxyPostWarpEnd, EDisplayClusterViewportResourceType::InputShaderResource, TextureShareDisplayClusterStrings::Viewport::Warped, true);

		ObjectProxy->FrameSync_RenderThread(RHICmdList, ETextureShareSyncStep::FrameProxyPostWarpEnd);
	}
}

void FDisplayClusterPostProcessTextureShare::HandleEndUpdateFrameResources_RenderThread(FRHICommandListImmediate& RHICmdList, const IDisplayClusterViewportManagerProxy* InViewportManagerProxy)
{
	check(IsInRenderingThread());

	if (IsActive() && ObjectProxy->IsFrameSyncActive_RenderThread())
	{
		// Share RTT with remote process
		ShareViewport_RenderThread(RHICmdList, InViewportManagerProxy, ETextureShareSyncStep::FrameProxyPostRenderEnd, EDisplayClusterViewportResourceType::OutputFrameTargetableResource,  TextureShareDisplayClusterStrings::Output::Viewport);
		ShareFrame_RenderThread(RHICmdList, InViewportManagerProxy, ETextureShareSyncStep::FrameProxyPostRenderEnd, EDisplayClusterViewportResourceType::OutputFrameTargetableResource,     TextureShareDisplayClusterStrings::Output::Backbuffer);
		ShareFrame_RenderThread(RHICmdList, InViewportManagerProxy, ETextureShareSyncStep::FrameProxyPostRenderEnd, EDisplayClusterViewportResourceType::AdditionalFrameTargetableResource, TextureShareDisplayClusterStrings::Output::BackbufferTemp);

		ObjectProxy->EndFrameSync_RenderThread(RHICmdList);
	}
}
