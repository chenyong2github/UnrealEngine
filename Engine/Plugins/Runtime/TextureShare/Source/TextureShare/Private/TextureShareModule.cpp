// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureShareModule.h"
#include "TextureShareRHI.h"
#include "TextureShareLog.h"

#include "PostProcess/SceneRenderTargets.h"

#include "ITextureShareItem.h"
#include "ITextureShareItemD3D11.h"
#include "ITextureShareItemD3D12.h"

#include "TextureShareCoreContainers.h"

#include "Blueprints/TextureShareContainers.h"

#include "TextureShareStrings.h"

FTextureShareModule::FTextureShareModule()
	: ShareCoreAPI(ITextureShareCore::Get())
{
	DisplayManager = MakeShareable(new FTextureShareDisplayManager(*this));
	UE_LOG(LogTextureShare, Log, TEXT("TextureShare module has been instantiated"));
}

FTextureShareModule::~FTextureShareModule()
{
	UE_LOG(LogTextureShare, Log, TEXT("TextureShare module has been destroyed"));
}

//////////////////////////////////////////////////////////////////////////////////////////////
// IModuleInterface
//////////////////////////////////////////////////////////////////////////////////////////////
void FTextureShareModule::StartupModule()
{
	UE_LOG(LogTextureShare, Log, TEXT("TextureShare module has started"));
}

void FTextureShareModule::ShutdownModule()
{
	ReleaseSharedResources();
	UE_LOG(LogTextureShare, Log, TEXT("TextureShare module shutdown"));
}

void FTextureShareModule::ReleaseSharedResources()
{
	FScopeLock lock(&DataGuard);

	TextureShareSceneContextCallback.Empty();
	DisplayManager->EndSceneSharing();

	ShareCoreAPI.ReleaseLib();
}

//////////////////////////////////////////////////////////////////////////////////////////////
// IModuleInterface
//////////////////////////////////////////////////////////////////////////////////////////////
void FTextureShareModule::RemoveSceneContextCallback(const FString& ShareName)
{
	TextureShareSceneContextCallback.Remove(ShareName.ToLower());

	if (TextureShareSceneContextCallback.Num() == 0)
	{
		DisplayManager->EndSceneSharing();
	}
}

bool FTextureShareModule::LinkSceneContextToShare(const TSharedPtr<ITextureShareItem>& ShareItem, int StereoViewIndex, bool bIsEnabled)
{
	if (ShareItem.IsValid() && ShareItem->IsValid() )
	{
		FScopeLock lock(&DataGuard);
		FString ShareName = ShareItem->GetName();

		if (bIsEnabled)
		{
			TextureShareSceneContextCallback.Emplace(ShareName.ToLower(), StereoViewIndex);
			DisplayManager->BeginSceneSharing();
		}
		else
		{
			RemoveSceneContextCallback(ShareName);
		}

		return true;
	}

	// Share not exist
	return false;
}

bool FTextureShareModule::SetBackbufferRect(int StereoViewIndex, const FIntRect* BackbufferRect)
{
	if (BackbufferRect == nullptr)
	{
		// Remove
		if (BackbufferRects.Contains(StereoViewIndex))
		{
			BackbufferRects.Remove(StereoViewIndex);
			return true;
		}
	}
	else
	{
		BackbufferRects.Emplace(StereoViewIndex, *BackbufferRect);
		return true;
	}

	//@ todo handle error
	return false;
}

ETextureShareDevice FTextureShareModule::GetTextureShareDeviceType() const
{
	FString RHIName = GDynamicRHI->GetName();
	if (RHIName == TEXT("D3D11"))
	{
		return ETextureShareDevice::D3D11;
	}
	else
		if (RHIName == TEXT("D3D12"))
		{
			return ETextureShareDevice::D3D12;
		}

	return ETextureShareDevice::Undefined;
};

bool FTextureShareModule::CreateShare(const FString& ShareName, const FTextureShareSyncPolicy& SyncMode, ETextureShareProcess Process, float SyncWaitTime)
{
	FScopeLock lock(&DataGuard);

	TSharedPtr<ITextureShareItem> ShareItem;
	ETextureShareDevice ShareDevice = GetTextureShareDeviceType();

	return ShareCoreAPI.CreateTextureShareItem(ShareName, Process, SyncMode, ShareDevice, ShareItem, SyncWaitTime);
}

bool FTextureShareModule::ReleaseShare(const FString& ShareName)
{
	if (ShareCoreAPI.ReleaseTextureShareItem(ShareName))
	{
		FScopeLock lock(&DataGuard);

		RemoveSceneContextCallback(ShareName);

		if (TextureShareSceneContextCallback.Num() == 0)
		{
			DisplayManager->EndSceneSharing();
		}
		return true;
	}

	return false;
}

bool FTextureShareModule::GetShare(const FString& ShareName, TSharedPtr<ITextureShareItem>& OutShareItem) const
{
	return ShareCoreAPI.GetTextureShareItem(ShareName, OutShareItem);
}

void FTextureShareModule::OnResolvedSceneColor_RenderThread(FRDGBuilder& GraphBuilder, const FSceneTextures& SceneTextures, class FSceneViewFamily& ViewFamily)
{
	FScopeLock lock(&DataGuard);

	if (ViewFamily.Views.Num() > 0)
	{
	{
		int32 StereoViewIndex = ViewFamily.Views[0]->StereoViewIndex;
		// Send SceneContext callback for all registered shares:
		for (auto& It : TextureShareSceneContextCallback)
		{
			if (It.Value == (int)StereoViewIndex)
			{
				SendSceneContext_RenderThread(GraphBuilder, It.Key, SceneTextures, ViewFamily);
			}
		}
	}
}
}

void FTextureShareModule::OnPostRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, class FSceneViewFamily& ViewFamily)
{
	FScopeLock lock(&DataGuard);
	
	if (ViewFamily.Views.Num() > 0)
	{
	{
		int32 StereoViewIndex = ViewFamily.Views[0]->StereoViewIndex;
		// Send PostRender callback for all registered shares:
		for (auto& It : TextureShareSceneContextCallback)
		{
			if (It.Value == (int)StereoViewIndex)
			{
				SendPostRender_RenderThread(RHICmdList, It.Key, ViewFamily);
			}
		}
	}
}
}

bool FTextureShareModule::RegisterTexture(const TSharedPtr<ITextureShareItem>& ShareItem, const FString& InTextureName, const FIntPoint& InSize, EPixelFormat InFormat, ETextureShareSurfaceOp OperationType)
{
	if(ShareItem.IsValid())
	{
		return ShareItem->RegisterTexture(InTextureName.ToLower(), InSize, ETextureShareFormat::Format_EPixel, InFormat, OperationType);
	}

	return false;
}

BEGIN_SHADER_PARAMETER_STRUCT(FSendTextureParameters, )
	RDG_TEXTURE_ACCESS(Texture, ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()

bool FTextureShareModule::SendSceneContext_RenderThread(FRDGBuilder& GraphBuilder, const FString& ShareName, const FSceneTextures& SceneTextures, class FSceneViewFamily& ViewFamily)
{
	TSharedPtr<ITextureShareItem> ShareItem;
	if (ShareCoreAPI.GetTextureShareItem(ShareName, ShareItem) && ShareItem.IsValid() && ShareItem->IsValid())
	{
		if (ShareItem->BeginFrame_RenderThread())
		{
			//@todo: Add additional information from ViewFamily
			if (ViewFamily.Views.Num() > 0)
			{
				const FSceneView* View = ViewFamily.Views[0];

				// Complete additional frame data
				{
					FTextureShareAdditionalData AdditionalData;
					{
						AdditionalData.FrameNumber = ViewFamily.FrameNumber;

						AdditionalData.PrjMatrix = View->ViewMatrices.GetProjectionMatrix();
						AdditionalData.ViewMatrix = View->ViewMatrices.GetViewMatrix();

						//@todo: Add more info
					}
					ShareItem->SetLocalAdditionalData(AdditionalData);
				}
			}

#if WITH_MGPU
			if (ViewFamily.bMultiGPUForkAndJoin)
			{
				if (ViewFamily.Views.Num() > 0)
				{
					// Setup GPU index for all shared scene textures
					const FSceneView* SceneView = ViewFamily.Views[0];
					ShareItem->SetDefaultGPUIndex(SceneView->GPUMask.GetFirstIndex());
				}
			}
#endif

			const auto AddSendTexturePass = [&](const TCHAR* ShareName, FRDGTextureRef Texture)
			{
				if (!HasBeenProduced(Texture))
				{
					return;
				}

				FSendTextureParameters* PassParameters = GraphBuilder.AllocParameters<FSendTextureParameters>();
				PassParameters->Texture = Texture;

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("SendTexture_%s", Texture->Name),
					PassParameters,
					ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
					[this, Texture, ShareItem, ShareName](FRHICommandListImmediate& RHICmdList)
				{
					SendTexture_RenderThread(RHICmdList, ShareItem, ShareName, Texture->GetRHI());
				});
			};

			AddSendTexturePass(TextureShareStrings::texture_name::SceneColor, SceneTextures.Color.Resolve);

			AddSendTexturePass(TextureShareStrings::texture_name::SceneDepth, SceneTextures.Depth.Resolve);
			AddSendTexturePass(TextureShareStrings::texture_name::SmallDepthZ, SceneTextures.SmallDepth);

			AddSendTexturePass(TextureShareStrings::texture_name::GBufferA, SceneTextures.GBufferA);
			AddSendTexturePass(TextureShareStrings::texture_name::GBufferB, SceneTextures.GBufferB);
			AddSendTexturePass(TextureShareStrings::texture_name::GBufferC, SceneTextures.GBufferC);
			AddSendTexturePass(TextureShareStrings::texture_name::GBufferD, SceneTextures.GBufferD);
			AddSendTexturePass(TextureShareStrings::texture_name::GBufferE, SceneTextures.GBufferE);
			AddSendTexturePass(TextureShareStrings::texture_name::GBufferF, SceneTextures.GBufferF);

			//@todo: Add more textures

			return true;
		}
	}

	return false;
}

bool FTextureShareModule::SendPostRender_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ShareName, class FSceneViewFamily& ViewFamily)
{
	TSharedPtr<ITextureShareItem> ShareItem;
	if (ShareCoreAPI.GetTextureShareItem(ShareName, ShareItem) && ShareItem.IsValid() && ShareItem->IsValid() && ShareItem->IsLocalFrameLocked())
	{
		if (ViewFamily.RenderTarget && ViewFamily.Views.Num() > 0)
		{
			// Get backbuffer texture
			FTexture2DRHIRef BackBufferTexture = ViewFamily.RenderTarget->GetRenderTargetTexture();

			//@todo Get rect from ViewFamily (check ViewFamily.Views[0]->UnconstrainedViewRect?, etc)
			// Use custom backbuffer viewport rect, defined by SetBackbufferRect()
			int32 StereoViewIndex = ViewFamily.Views[0]->StereoViewIndex;

			// Send rect of backbuffer texture
			SendTexture_RenderThread(RHICmdList, ShareItem, TextureShareStrings::texture_name::BackBuffer, BackBufferTexture.GetReference(), BackbufferRects.Find(StereoViewIndex));
		}

		ShareItem->EndFrame_RenderThread();
		return true;
	}

	return false;
}

bool FTextureShareModule::SendTexture_RenderThread(FRHICommandListImmediate& RHICmdList, const TSharedPtr<ITextureShareItem>& ShareItem, const FString& InTextureName, FRHITexture* RHITexture, const FIntRect* SrcTextureRect)
{
	if (ShareItem.IsValid() && ShareItem->IsValid())
	{
		FString TextureName = InTextureName.ToLower();
		if (ShareItem->IsRemoteTextureUsed(TextureName))
		{
			if (RHITexture && RHITexture->IsValid())
			{
				// register size+format, then send texture
				FIntVector InSize = RHITexture->GetSizeXYZ();
				FIntPoint  SrcRectSize = SrcTextureRect ? SrcTextureRect->Size() : FIntPoint(InSize.X, InSize.Y);

				if (RegisterTexture(ShareItem, TextureName, SrcRectSize, RHITexture->GetFormat(), ETextureShareSurfaceOp::Write))
				{
					return WriteToShare_RenderThread(RHICmdList, ShareItem, TextureName, RHITexture, SrcTextureRect);
				}
			}
		}
	}

	return false;
}

bool FTextureShareModule::WriteToShare_RenderThread(FRHICommandListImmediate& RHICmdList, const TSharedPtr<ITextureShareItem>& ShareItem, const FString& InTextureName, FRHITexture* SrcTexture, const FIntRect* SrcTextureRect)
{
	bool bResult = false;
	if (ShareItem.IsValid())
	{
		FTexture2DRHIRef SharedRHITexture;
		FString TextureName = InTextureName.ToLower();
		if (ShareItem->LockRHITexture_RenderThread(TextureName, SharedRHITexture))
		{
			bool bIsFormatResampleRequired = ShareItem->IsFormatResampleRequired(SharedRHITexture, SrcTexture);
			bResult = FTextureShareRHI::WriteToShareTexture_RenderThread(RHICmdList, SrcTexture, SharedRHITexture, SrcTextureRect, bIsFormatResampleRequired);
			ShareItem->TransferTexture_RenderThread(RHICmdList, TextureName);
			ShareItem->UnlockTexture_RenderThread(TextureName);
		}
	}

	return bResult;
}

bool FTextureShareModule::ReceiveTexture_RenderThread(FRHICommandListImmediate& RHICmdList, const TSharedPtr<ITextureShareItem>& ShareItem, const FString& TextureName, FRHITexture* RHITexture, const FIntRect* DstTextureRect)
{
	if (RHITexture && RHITexture->IsValid())
	{
		// register size+format, then receive texture
		FIntVector InSize = RHITexture->GetSizeXYZ();
		FIntPoint  DstRectSize = DstTextureRect ? DstTextureRect->Size() : FIntPoint(InSize.X, InSize.Y);

		if (RegisterTexture(ShareItem, TextureName, DstRectSize, RHITexture->GetFormat(), ETextureShareSurfaceOp::Read))
		{
			return ReadFromShare_RenderThread(RHICmdList, ShareItem, TextureName, RHITexture, DstTextureRect);
		}
	}
	return false;
}

bool FTextureShareModule::ReadFromShare_RenderThread(FRHICommandListImmediate& RHICmdList, const TSharedPtr<ITextureShareItem>& ShareItem, const FString& InTextureName, FRHITexture* DstTexture, const FIntRect* DstTextureRect)
{
	bool bResult = false;

	if (ShareItem.IsValid())
	{
		FTexture2DRHIRef SharedRHITexture;
		FString TextureName = InTextureName.ToLower();
		if (ShareItem->LockRHITexture_RenderThread(TextureName, SharedRHITexture))
		{
			bool bIsFormatResampleRequired = ShareItem->IsFormatResampleRequired(SharedRHITexture, DstTexture);
			ShareItem->TransferTexture_RenderThread(RHICmdList, TextureName);
			bResult = FTextureShareRHI::ReadFromShareTexture_RenderThread(RHICmdList, SharedRHITexture, DstTexture, DstTextureRect, bIsFormatResampleRequired);
			ShareItem->UnlockTexture_RenderThread(TextureName);
		}
	} 
	return bResult;
}

void FTextureShareModule::CastTextureShareBPSyncPolicy(const FTextureShareBPSyncPolicy& InSyncPolicy, FTextureShareSyncPolicy& OutSyncPolicy)
{
	OutSyncPolicy = *InSyncPolicy;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// ITextureShare
//////////////////////////////////////////////////////////////////////////////////////////////
IMPLEMENT_MODULE(FTextureShareModule, TextureShare);
