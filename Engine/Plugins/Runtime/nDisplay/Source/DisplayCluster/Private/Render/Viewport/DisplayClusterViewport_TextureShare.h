// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if PLATFORM_WINDOWS
#include "Containers/TextureShareCoreGenericContainers.h"
#endif

struct FDisplayClusterConfigurationTextureShare_Viewport;
struct FTextureShareSyncPolicyDisplayCluster;
struct FDisplayClusterTextureShareSettings;
class FDisplayClusterViewport;
class ITextureShareItem;

class FDisplayClusterViewport_TextureShare
{
	struct FTextureShareConfiguration
	{
	public:
		FTextureShareConfiguration()
		{ }

#if PLATFORM_WINDOWS
		FTextureShareConfiguration(const FString& InTextureShareId, const FTextureShareSyncPolicy& InSyncSettings)
			: TextureShareId(InTextureShareId)
			, SyncSettings(InSyncSettings)
		{ }

		bool operator==(const FTextureShareConfiguration& InCfg) const
		{
			return TextureShareId == InCfg.TextureShareId
				&& SyncSettings == InCfg.SyncSettings;
		}

	public:
		FString TextureShareId;
		FTextureShareSyncPolicy SyncSettings;

		void Empty()
		{
			TextureShareId.Empty();
		}

		bool IsValid() const
		{
			return !TextureShareId.IsEmpty();
		}
#endif
	};

public:
	FDisplayClusterViewport_TextureShare();
	~FDisplayClusterViewport_TextureShare();

public:
	bool Get(TSharedPtr<ITextureShareItem>& OutTextureShareItem) const;

	bool UpdateConfiguration(const FDisplayClusterViewport& InViewport, const FDisplayClusterConfigurationTextureShare_Viewport& InConfiguration);
	bool UpdateLinkSceneContextToShare(const FDisplayClusterViewport& InViewport);

	static bool BeginSyncFrame(const FDisplayClusterTextureShareSettings& InSettings);
	static bool EndSyncFrame(const FDisplayClusterTextureShareSettings& InSettings);

private:
	bool ImplCreate(const FTextureShareConfiguration& InConfiguration);
	bool ImplDelete();

private:
	FTextureShareConfiguration ExistConfiguration;
	FTextureShareConfiguration NewConfiguration;
};
