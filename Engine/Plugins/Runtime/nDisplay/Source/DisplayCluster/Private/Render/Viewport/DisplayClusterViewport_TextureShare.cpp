// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Viewport/DisplayClusterViewport_TextureShare.h"
#include "Render/Viewport/DisplayClusterViewport.h"
#include "Render/Viewport/DisplayClusterViewportManager.h"

#if PLATFORM_WINDOWS
#include "Containers/TextureShareCoreEnums.h"
#include "Blueprints/TextureShareContainers.h"
#include "ITextureShare.h"
#include "ITextureShareCore.h"
#include "ITextureShareItem.h"
#endif

#include "Render/Viewport/Containers/DisplayClusterTextureShareSettings.h"
#include "Render/Viewport/Configuration/DisplayClusterViewportConfiguration.h"

#include "DisplayClusterConfigurationTypes_TextureShare.h"
#include "Misc/DisplayClusterLog.h"

#if PLATFORM_WINDOWS
static ITextureShare& GetTextureShareAPI()
{
	static ITextureShare& SingletonTextureShareApi = ITextureShare::Get();
	return SingletonTextureShareApi;
}

static ITextureShareCore& GetTextureShareCoreAPI()
{
	static ITextureShareCore& SingletonTextureShareCoreApi = ITextureShareCore::Get();
	return SingletonTextureShareCoreApi;
}

static FTextureShareSyncPolicy ImplGetSyncPolicy(const FTextureShareSyncPolicyDisplayCluster& InSyncPolicy)
{
	FTextureShareSyncPolicy SyncPolicy;

	// sync settings
	switch (InSyncPolicy.Connection)
	{
	case ETextureShareSyncConnectDisplayCluster::Default: SyncPolicy.ConnectionSync = ETextureShareSyncConnect::Default; break;
	case ETextureShareSyncConnectDisplayCluster::None: SyncPolicy.ConnectionSync = ETextureShareSyncConnect::None; break;
	case ETextureShareSyncConnectDisplayCluster::SyncSession: SyncPolicy.ConnectionSync = ETextureShareSyncConnect::SyncSession; break;
	default: UE_LOG(LogDisplayClusterViewport, Error, TEXT("Unsupported texture share sync connection type"));  break;
	}

	switch (InSyncPolicy.Frame)
	{
	case ETextureShareSyncFrameDisplayCluster::Default: SyncPolicy.FrameSync = ETextureShareSyncFrame::Default; break;
	case ETextureShareSyncFrameDisplayCluster::None: SyncPolicy.FrameSync = ETextureShareSyncFrame::None; break;
	case ETextureShareSyncFrameDisplayCluster::FrameSync: SyncPolicy.FrameSync = ETextureShareSyncFrame::FrameSync; break;
	default: UE_LOG(LogDisplayClusterViewport, Error, TEXT("Unsupported texture share sync frame type"));  break; break;
	}

	switch (InSyncPolicy.Texture)
	{
	case ETextureShareSyncSurfaceDisplayCluster::Default: SyncPolicy.TextureSync = ETextureShareSyncSurface::Default; break;
	case ETextureShareSyncSurfaceDisplayCluster::None: SyncPolicy.TextureSync = ETextureShareSyncSurface::None; break;
	case ETextureShareSyncSurfaceDisplayCluster::SyncRead: SyncPolicy.TextureSync = ETextureShareSyncSurface::SyncRead; break;
	case ETextureShareSyncSurfaceDisplayCluster::SyncPairingRead: SyncPolicy.TextureSync = ETextureShareSyncSurface::SyncPairingRead; break;
	default: UE_LOG(LogDisplayClusterViewport, Error, TEXT("Unsupported texture share sync texture type"));  break;
	}

	return SyncPolicy;
}
#endif

//-------------------------------------------------------------------------------
// FDisplayClusterViewport_TextureShare
//-------------------------------------------------------------------------------
FDisplayClusterViewport_TextureShare::FDisplayClusterViewport_TextureShare()
{

}

FDisplayClusterViewport_TextureShare::~FDisplayClusterViewport_TextureShare()
{
	ImplDelete();
}

bool FDisplayClusterViewport_TextureShare::ImplDelete()
{
#if PLATFORM_WINDOWS
	if (ExistConfiguration.IsValid())
	{
		FString ExistTextureShareId = ExistConfiguration.TextureShareId;
		ExistConfiguration.Empty();

		if(GetTextureShareAPI().ReleaseShare(ExistTextureShareId))
		{
			return true;
		}

		UE_LOG(LogDisplayClusterViewport, Error, TEXT("Failed delete viewport share '%s'"), *ExistTextureShareId);
	}
#endif

	return false;
}

bool FDisplayClusterViewport_TextureShare::ImplCreate(const FTextureShareConfiguration& InConfiguration)
{
#if PLATFORM_WINDOWS
	if (!ExistConfiguration.IsValid())
	{
		if (GetTextureShareAPI().CreateShare(InConfiguration.TextureShareId, InConfiguration.SyncSettings, ETextureShareProcess::Server))
		{
			ExistConfiguration = InConfiguration;
			return true;
		}

		UE_LOG(LogDisplayClusterViewport, Error, TEXT("Failed create viewport share '%s'"), *InConfiguration.TextureShareId);
	}
#endif

	return false;
}

bool FDisplayClusterViewport_TextureShare::Get(TSharedPtr<ITextureShareItem>& OutTextureShareItem) const
{
#if PLATFORM_WINDOWS
	if (ExistConfiguration.IsValid())
	{
		return GetTextureShareAPI().GetShare(ExistConfiguration.TextureShareId, OutTextureShareItem);
	}
#endif

	return false;
}

bool FDisplayClusterViewport_TextureShare::UpdateLinkSceneContextToShare(const FDisplayClusterViewport& InViewport)
{
#if PLATFORM_WINDOWS
	const TArray<FDisplayClusterViewport_Context>& InContexts = InViewport.GetContexts();
	// Now share only mono/left eye
	if (InContexts.Num() > 0)
	{
		// This viewport in render
		const FDisplayClusterViewport_Context& InContext = InContexts[0];
		if (!InContext.bDisableRender)
		{
			// Update texture share configuration on fly
			if (NewConfiguration.IsValid())
			{
				ImplDelete();
				if (!ImplCreate(NewConfiguration))
				{
					// MU issue: on recompile destructor not called for ViewportManager from RootActor
					// fix it: delete
					ExistConfiguration = NewConfiguration;
					ImplDelete();
					if (!ImplCreate(NewConfiguration))
					{
						return false;
					}
				}
				NewConfiguration.Empty();
			}

			if (ExistConfiguration.IsValid())
			{
				int PassType = InContext.StereoscopicPass;

				TSharedPtr<ITextureShareItem> ShareItem;
				if (GetTextureShareAPI().GetShare(ExistConfiguration.TextureShareId, ShareItem))
				{
					if (GetTextureShareAPI().LinkSceneContextToShare(ShareItem, PassType, true))
					{
						// Map viewport rect to stereoscopic pass
						GetTextureShareAPI().SetBackbufferRect(PassType, &InContext.RenderTargetRect);

						// Begin share session
						if (!ShareItem->IsSessionValid())
						{
							ShareItem->BeginSession();
						}

						return true;
					}
					else
					{
						UE_LOG(LogDisplayClusterViewport, Error, TEXT("failed link scene context for share '%s'"), *ExistConfiguration.TextureShareId);
					}
				}
				else
				{
					UE_LOG(LogDisplayClusterViewport, Error, TEXT("UpdateLinkSceneContextToShare: share '%s' not exist. Remove"), *ExistConfiguration.TextureShareId);
				}
			}
		}
	}

	// remove textureshare for invisible or disabled viewports
	ImplDelete();
	NewConfiguration.Empty();

#endif

	return false;
}

bool FDisplayClusterViewport_TextureShare::UpdateConfiguration(const FDisplayClusterViewport& InViewport, const FDisplayClusterConfigurationTextureShare_Viewport& InConfiguration)
{
#if PLATFORM_WINDOWS
	if (InConfiguration.bIsEnabled)
	{
		FTextureShareConfiguration InCfg(InViewport.GetId(), ImplGetSyncPolicy(InConfiguration.SyncSettings));
		bool bIsConfigurationEqual = InCfg == ExistConfiguration;

		if (!bIsConfigurationEqual || NewConfiguration.IsValid())
		{
			// Update share with new cfg delayed
			NewConfiguration = InCfg;
		}

		return true;
	}

	// remove unused share
	return ImplDelete();
#endif
	return false;
}

bool FDisplayClusterViewport_TextureShare::BeginSyncFrame(const FDisplayClusterTextureShareSettings& InSettings)
{
#if PLATFORM_WINDOWS
	if (InSettings.bIsGlobalSyncEnabled && InSettings.bIsEnabled)
	{
		return GetTextureShareCoreAPI().BeginSyncFrame();
	}
#endif

	return false;
}

bool FDisplayClusterViewport_TextureShare::EndSyncFrame(const FDisplayClusterTextureShareSettings& InSettings)
{
#if PLATFORM_WINDOWS
	if (InSettings.bIsGlobalSyncEnabled && InSettings.bIsEnabled)
	{
		return GetTextureShareCoreAPI().EndSyncFrame();
	}
#endif

	return false;
}

