// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Viewport/DisplayClusterViewport_TextureShare.h"
#include "Render/Viewport/DisplayClusterViewport.h"

#if PLATFORM_WINDOWS
	#include "Containers/TextureShareCoreEnums.h"
	#include "Blueprints/TextureShareContainers.h"
	#include "ITextureShare.h"
	#include "ITextureShareItem.h"
#endif

#include "DisplayClusterConfigurationTypes_TextureShare.h"
#include "Misc/DisplayClusterLog.h"

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

		if(ITextureShare::Get().ReleaseShare(ExistTextureShareId))
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
		if (ITextureShare::Get().CreateShare(InConfiguration.TextureShareId, InConfiguration.SyncSettings, ETextureShareProcess::Server))
		{
			ExistConfiguration = InConfiguration;
			return true;
		}

		UE_LOG(LogDisplayClusterViewport, Error, TEXT("Failed create viewport share '%s'"), *InConfiguration.TextureShareId);
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
		const FDisplayClusterViewport_Context& InContext = InContexts[0];
		if (!InContext.bDisableRender)
		{
			// This viewport in render

			// Update texture share configuration on fly
			if (NewConfiguration.IsValid())
			{
				ImplDelete();
				bool bIsTextureShareCreated = ImplCreate(NewConfiguration);

				if(!bIsTextureShareCreated)
				{
					// MU issue: on recompile destructor not called for ViewportManager from RootActor
					// fix it
					ExistConfiguration = NewConfiguration;
					ImplDelete();
					bool bIsTextureShareCreatedFixed = ImplCreate(NewConfiguration);
					if (!bIsTextureShareCreatedFixed)
					{
						return false;
					}
				}

				NewConfiguration.Empty();
			}

			if(ExistConfiguration.IsValid())
			{
				int PassType = InContext.StereoscopicPass;

				static ITextureShare& TextureShareAPI = ITextureShare::Get();
				TSharedPtr<ITextureShareItem> ShareItem;
				if (TextureShareAPI.GetShare(ExistConfiguration.TextureShareId, ShareItem))
				{
					if (TextureShareAPI.LinkSceneContextToShare(ShareItem, PassType, true))
					{
						// Map viewport rect to stereoscopic pass
						TextureShareAPI.SetBackbufferRect(PassType, &InContext.RenderTargetRect);

						// Begin share session
						if (!ShareItem->IsSessionValid())
						{
							ShareItem->BeginSession();
						}

						return true;
					}
					else
					{
						UE_LOG(LogDisplayClusterViewport, Error, TEXT("failed link scene conext for share '%s'"), *ExistConfiguration.TextureShareId);
					}
				}
				else
				{
					UE_LOG(LogDisplayClusterViewport, Error, TEXT("UpdateLinkSceneContextToShare: share '%s' not exist. Remove"), *ExistConfiguration.TextureShareId);
				}
			}
		}
	}

	//
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
		FTextureShareSyncPolicy SyncPolicy;
		FTextureShareBPSyncPolicy SyncSettings;

		// sync settings
		switch(InConfiguration.SyncSettings.Connection)
		{
			case ETextureShareSyncConnectDisplayCluster::Default: SyncSettings.Connection = ETextureShareBPSyncConnect::Default; break;
			case ETextureShareSyncConnectDisplayCluster::None: SyncSettings.Connection = ETextureShareBPSyncConnect::None; break;
			case ETextureShareSyncConnectDisplayCluster::SyncSession: SyncSettings.Connection = ETextureShareBPSyncConnect::SyncSession; break;
			default: UE_LOG(LogDisplayClusterViewport, Error, TEXT("Unsupported texture share sync connection type"));  break;
		}

		switch (InConfiguration.SyncSettings.Frame)
		{
			case ETextureShareSyncFrameDisplayCluster::Default: SyncSettings.Frame = ETextureShareBPSyncFrame::Default; break;
			case ETextureShareSyncFrameDisplayCluster::None: SyncSettings.Frame = ETextureShareBPSyncFrame::None; break;
			case ETextureShareSyncFrameDisplayCluster::FrameSync: SyncSettings.Frame = ETextureShareBPSyncFrame::FrameSync; break;
			default: UE_LOG(LogDisplayClusterViewport, Error, TEXT("Unsupported texture share sync frame type"));  break; break;
		}

		switch (InConfiguration.SyncSettings.Texture)
		{
			case ETextureShareSyncSurfaceDisplayCluster::Default: SyncSettings.Texture = ETextureShareBPSyncSurface::Default; break;
			case ETextureShareSyncSurfaceDisplayCluster::None: SyncSettings.Texture = ETextureShareBPSyncSurface::None; break;
			case ETextureShareSyncSurfaceDisplayCluster::SyncRead: SyncSettings.Texture = ETextureShareBPSyncSurface::SyncRead; break;
			case ETextureShareSyncSurfaceDisplayCluster::SyncPairingRead: SyncSettings.Texture = ETextureShareBPSyncSurface::SyncPairingRead; break;
			default: UE_LOG(LogDisplayClusterViewport, Error, TEXT("Unsupported texture share sync texture type"));  break;
		}

		static ITextureShare& TextureShareAPI = ITextureShare::Get();
		TextureShareAPI.CastTextureShareBPSyncPolicy(SyncSettings, SyncPolicy);

		FTextureShareConfiguration InCfg(InViewport.GetId(), SyncPolicy);
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
