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

//////////////////////////////////////////////////////////////////////////////////////////////
static ITextureShare& TextureShareAPI()
{
	static ITextureShare& TextureShareAPISingleton = ITextureShare::Get();
	return TextureShareAPISingleton;
}

static ITextureShareCore& TextureShareCoreAPI()
{
	static ITextureShareCore& TextureShareCoreAPISingleton = ITextureShareCore::Get();
	return TextureShareCoreAPISingleton;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// FTextureShareTexturesArgsHelper
//////////////////////////////////////////////////////////////////////////////////////////////
struct FTextureShareTexturesArgsHelper
{
	TArray<FTextureShareBPTexture2D> Send;
	TArray<FTextureShareBPTexture2D> Receive;
	FTextureShareBPAdditionalData    AdditionalData;

	FTextureShareTexturesArgsHelper(const FTextureShareBPPostprocess& Postprocess)
		: Send(Postprocess.Send)
		, Receive(Postprocess.Receive)
		, AdditionalData(Postprocess.AdditionalData)
	{}

	void SendTextures(FRHICommandListImmediate& RHICmdList, TSharedPtr<ITextureShareItem>& ShareItem)
	{
		for (FTextureShareBPTexture2D& SendTextureIt : Send)
		{
			FTexture2DRHIRef Texture2DRHIRef;
			if (SendTextureIt.GetTexture2DRHI_RenderThread(Texture2DRHIRef))
			{
				// Send Texture
				TextureShareAPI().SendTexture_RenderThread(RHICmdList, ShareItem, SendTextureIt.Id, Texture2DRHIRef);
			}
		}
	}

	void ReceiveTextures(FRHICommandListImmediate& RHICmdList, TSharedPtr<ITextureShareItem>& ShareItem)
	{
		for (FTextureShareBPTexture2D& ReceiveTextureIt : Receive)
		{
			FTexture2DRHIRef Texture2DRHIRef;
			if (ReceiveTextureIt.GetTexture2DRHI_RenderThread(Texture2DRHIRef))
			{
				TextureShareAPI().ReceiveTexture_RenderThread(RHICmdList, ShareItem, ReceiveTextureIt.Id, Texture2DRHIRef);
			}
		}
	}

	void PreRegisterReceiveTextures(FRHICommandListImmediate& RHICmdList, TSharedPtr<ITextureShareItem>& ShareItem)
	{
		for (FTextureShareBPTexture2D& ReceiveTextureIt : Receive)
		{
			FTexture2DRHIRef Texture2DRHIRef;
			if (ReceiveTextureIt.GetTexture2DRHI_RenderThread(Texture2DRHIRef))
			{
				FIntVector InSize = Texture2DRHIRef->GetSizeXYZ();
				FIntPoint  DstRectSize = FIntPoint(InSize.X, InSize.Y);

				TextureShareAPI().RegisterTexture(ShareItem, ReceiveTextureIt.Id, DstRectSize, Texture2DRHIRef->GetFormat(), ETextureShareSurfaceOp::Read);
			}
		}
	}

	void ApplyPostprocess_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ShareName)
	{
		check(IsInRenderingThread());

		TSharedPtr<ITextureShareItem> ShareItem;
		if (TextureShareAPI().GetShare(ShareName, ShareItem))
		{
			// Setup texture for read before frame
			PreRegisterReceiveTextures(RHICmdList, ShareItem);

			if (ShareItem->BeginFrame_RenderThread())
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
	}
};

//////////////////////////////////////////////////////////////////////////////////////////////
// UTextureShareAPIImpl
//////////////////////////////////////////////////////////////////////////////////////////////
FTextureShareBPSyncPolicySettings UTextureShareAPIImpl::GetSyncPolicySettings() const
{
	return FTextureShareBPSyncPolicySettings(TextureShareCoreAPI().GetSyncPolicySettings(ETextureShareProcess::Server));
}

void UTextureShareAPIImpl::SetSyncPolicySettings(const FTextureShareBPSyncPolicySettings& InSyncPolicySettings)
{
	TextureShareCoreAPI().SetSyncPolicySettings(ETextureShareProcess::Server, *InSyncPolicySettings);
}

bool UTextureShareAPIImpl::CreateTextureShare(const FString ShareName, FTextureShareBPSyncPolicy SyncMode, bool bIsServer, float SyncWaitTime)
{
	return TextureShareAPI().CreateShare(ShareName, *SyncMode, bIsServer ? ETextureShareProcess::Server : ETextureShareProcess::Client, SyncWaitTime);
}

bool UTextureShareAPIImpl::ReleaseTextureShare(const FString ShareName)
{
	return TextureShareAPI().ReleaseShare(ShareName);
}

bool UTextureShareAPIImpl::LinkSceneContextToShare(const FString ShareName, int StereoscopicPass, bool bIsEnabled)
{
	TSharedPtr<ITextureShareItem> ShareItem;
	if (TextureShareAPI().GetShare(ShareName, ShareItem))
	{
		if (bIsEnabled)
		{
			ShareItem->BeginSession();
		}
		else
		{
			ShareItem->EndSession();
		}

		return TextureShareAPI().LinkSceneContextToShare(ShareItem, StereoscopicPass, bIsEnabled);
	}

	return false;
}

bool UTextureShareAPIImpl::ApplyTextureSharePostprocess(const FString ShareName, const FTextureShareBPPostprocess& Postprocess)
{
	TSharedPtr<ITextureShareItem> ShareItem;
	if (TextureShareAPI().GetShare(ShareName, ShareItem))
	{
		// Begin session once
		if (!ShareItem->IsSessionValid())
		{
			if (!ShareItem->BeginSession())
			{
				TextureShareAPI().ReleaseShare(ShareName);
				UE_LOG(LogTextureShareBP, Error, TEXT("Can't BeginSession() for share '%s'. Share deleted."), *ShareName);
				return false;
			}
		}

		// Forward textures update to Render thread:
		{
			FTextureShareTexturesArgsHelper* PPArgs = new FTextureShareTexturesArgsHelper(Postprocess);
			ENQUEUE_RENDER_COMMAND(TextureShare_ApplyPP)(
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
