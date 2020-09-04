// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprints/TextureShareBlueprintAPIImpl.h"
#include "UObject/Package.h"

#include "ITextureShare.h"
#include "ITextureShareCore.h"
#include "ITextureShareItem.h"
#include "TextureShareLog.h"

#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "RHIUtilities.h"

#include "CommonRenderResources.h"
#include "Engine/TextureRenderTarget2D.h"

ITextureShare& GetAPI()
{
	static ITextureShare& TextureShareAPI = ITextureShare::Get();
	return TextureShareAPI;
}

FTextureShareBPSyncPolicySettings UTextureShareAPIImpl::GetSyncPolicySettings() const
{
	return FTextureShareBPSyncPolicySettings(ITextureShareCore::Get().GetSyncPolicySettings(ETextureShareProcess::Server));
}

void UTextureShareAPIImpl::SetSyncPolicySettings(const FTextureShareBPSyncPolicySettings& InSyncPolicySettings)
{
	ITextureShareCore::Get().SetSyncPolicySettings(ETextureShareProcess::Server, *InSyncPolicySettings);
}


bool UTextureShareAPIImpl::CreateTextureShare(const FString ShareName, FTextureShareBPSyncPolicy SyncMode, bool bIsServer)
{
	return GetAPI().CreateShare(ShareName, *SyncMode, bIsServer ? ETextureShareProcess::Server : ETextureShareProcess::Client);
}

bool UTextureShareAPIImpl::ReleaseTextureShare(const FString ShareName)
{
	return GetAPI().ReleaseShare(ShareName);
}

bool UTextureShareAPIImpl::LinkSceneContextToShare(const FString ShareName, int StereoscopicPass, bool bIsEnabled)
{
	TSharedPtr<ITextureShareItem> ShareItem;
	if (GetAPI().GetShare(ShareName, ShareItem))
	{
		if (bIsEnabled)
		{
			ShareItem->BeginSession();
		}
		else
		{
			ShareItem->EndSession();
		}

		return GetAPI().LinkSceneContextToShare(ShareItem, StereoscopicPass, bIsEnabled);
	}

	return false;
}

struct FTextureShareTexturesArgs
{
	TArray<FTextureShareBPTexture2D> Send;
	TArray<FTextureShareBPTexture2D> Receive;
	FTextureShareBPAdditionalData    AdditionalData;

	FTextureShareTexturesArgs(const FTextureShareBPPostprocess& Postprocess)
		: Send(Postprocess.Send)
		, Receive(Postprocess.Receive)
		, AdditionalData(Postprocess.AdditionalData)
	{}

	void SendTextures(FRHICommandListImmediate& RHICmdList, TSharedPtr<ITextureShareItem>& ShareItem)
	{
		for (auto WriteIt : Send)
		{
			FTexture2DRHIRef Texture2DRHIRef;
			if (WriteIt.GetTexture2DRHI_RenderThread(Texture2DRHIRef))
			{
				// Send Texture
				GetAPI().SendTexture_RenderThread(RHICmdList, ShareItem, WriteIt.Id, Texture2DRHIRef);
			}
		}
	}
	
	void ReceiveTextures(FRHICommandListImmediate& RHICmdList, TSharedPtr<ITextureShareItem>& ShareItem)
	{
		for (auto ReadIt : Receive)
		{
			FTexture2DRHIRef Texture2DRHIRef;
			if (ReadIt.GetTexture2DRHI_RenderThread(Texture2DRHIRef))
			{
				GetAPI().ReceiveTexture_RenderThread(RHICmdList, ShareItem, ReadIt.Id, Texture2DRHIRef);
			}
		}
	}

	void ApplyPostprocess_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ShareName)
	{
		TSharedPtr<ITextureShareItem> ShareItem;
		if (GetAPI().GetShare(ShareName, ShareItem) && ShareItem->BeginFrame_RenderThread())
		{
			if (ShareItem->IsClient())
			{
				ReceiveTextures(RHICmdList, ShareItem);
				SendTextures(RHICmdList, ShareItem);
			}
			else
			{
				SendTextures(RHICmdList, ShareItem);
				ReceiveTextures(RHICmdList, ShareItem);
			}

			// Send frame additional data
			ShareItem->SetLocalAdditionalData(*AdditionalData);

			ShareItem->EndFrame_RenderThread();
		}
	}
};

bool UTextureShareAPIImpl::ApplyTextureSharePostprocess(const FString ShareName, const FTextureShareBPPostprocess& Postprocess)
{
	TSharedPtr<ITextureShareItem> ShareItem;
	if (GetAPI().GetShare(ShareName, ShareItem))
	{
		// Begin session once
		if (!ShareItem->IsSessionValid())
		{
			if (!ShareItem->BeginSession())
			{
				GetAPI().ReleaseShare(ShareName);
				UE_LOG(LogTextureShareBP, Error, TEXT("Can't BeginSession() for share '%s'. Share deleted."), *ShareName);
				return false;
			}
		}

		// Forward textures update to Render thread:
		{
			FTextureShareTexturesArgs* PPArgs = new FTextureShareTexturesArgs(Postprocess);
			ENQUEUE_RENDER_COMMAND(void)(
				[ShareName, PPArgs](FRHICommandListImmediate& RHICmdList)
			{
				PPArgs->ApplyPostprocess_RenderThread(RHICmdList, ShareName);
				delete PPArgs;
			});
		}

		return true;
	}

	return false;
}

