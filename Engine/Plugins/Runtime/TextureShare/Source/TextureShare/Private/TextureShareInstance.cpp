// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureShareInstance.h"
#include "TextureShareRHI.h"
#include "TextureShareLog.h"

#include "ITextureShareItem.h"
#include "ITextureShareItemD3D11.h"
#include "ITextureShareItemD3D12.h"

#include "TextureShareCoreContainers.h"
#include "TextureShareDisplayManager.h"
#include "TextureShareStrings.h"

#include "PostProcess/SceneRenderTargets.h"

//////////////////////////////////////////////////////////////////////////////////////////////
static ITextureShareCore& GetTextureShareCoreAPI()
{
	static ITextureShareCore& TextureShareCoreAPISingleton = ITextureShareCore::Get();
	return TextureShareCoreAPISingleton;
}

ETextureShareDevice ImplGetTextureShareDeviceType()
{
	static ETextureShareDevice CurrentRenderDevice = ETextureShareDevice::Undefined;

	if (CurrentRenderDevice == ETextureShareDevice::Undefined)
	{
		FString RHIName = GDynamicRHI->GetName();
		if (RHIName == TEXT("D3D11"))
		{
			CurrentRenderDevice = ETextureShareDevice::D3D11;
		}
		else if (RHIName == TEXT("D3D12"))
		{
			CurrentRenderDevice = ETextureShareDevice::D3D12;
		}
		else
		{
			CurrentRenderDevice = ETextureShareDevice::Undefined;
		}
	}

	return CurrentRenderDevice;
};

//////////////////////////////////////////////////////////////////////////////////////////////
// FTextureShareInstanceData
//////////////////////////////////////////////////////////////////////////////////////////////
void FTextureShareInstanceData::ResetSceneContext()
{
	if (DisplayManager.IsValid())
	{
		// if this is last reference to DM, end scene sharing
		int32 RefCount = DisplayManager.GetSharedReferenceCount();
		if (RefCount == 2)
		{
			// FTextureShareModule always has 1 ref
			DisplayManager->EndSceneSharing();
		}
	}

	StereoscopicPass = -1;
	DisplayManager.Reset();
}

void FTextureShareInstanceData::LinkSceneContext(int32 InStereoscopicPass, const TSharedPtr<FTextureShareDisplayManager, ESPMode::ThreadSafe>& InDisplayManager)
{
	if (InStereoscopicPass < 0)
	{
		ResetSceneContext();
	}
	else
	{
		StereoscopicPass = InStereoscopicPass;

		DisplayManager = InDisplayManager;
		if (DisplayManager.IsValid())
		{
			DisplayManager->BeginSceneSharing();
		}
	}
}

void FTextureShareInstanceData::SetRTTRect(const FIntRect* InRTTRect)
{
	if (InRTTRect)
	{
		RTTRect = *InRTTRect;
		bAllowRTTRect = true;
	}
	else
	{
		bAllowRTTRect = false;
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////
// FTextureShareInstance
//////////////////////////////////////////////////////////////////////////////////////////////
bool FTextureShareInstance::Create(TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& OutShareInstance, const FString& ShareName, const FTextureShareSyncPolicy& SyncMode, ETextureShareProcess Process)
{
	// SyncWaitTime now is deprecated
	const float SyncWaitTime = 0;

	TSharedPtr<ITextureShareItem> ShareItem;
	if (GetTextureShareCoreAPI().CreateTextureShareItem(ShareName, Process, SyncMode, ImplGetTextureShareDeviceType(), ShareItem, SyncWaitTime))
	{
		OutShareInstance = MakeShared<FTextureShareInstance, ESPMode::ThreadSafe>(ShareName, ShareItem);
		return OutShareInstance.IsValid() && OutShareInstance->IsValid();
	}

	return false;
}

FTextureShareInstance::~FTextureShareInstance()
{
	// Destructor called from both game and render threads
	InstanceData.ResetSceneContext();
	InstanceData_RenderThread.ResetSceneContext();

	// Release texture share:
	ShareItem.Reset();
	GetTextureShareCoreAPI().ReleaseTextureShareItem(ShareName);
}

void FTextureShareInstance::HandleBeginNewFrame()
{
	// update current frame number
	ImplGetData().FrameNumber++;

	if (ShareItem.IsValid())
	{
		// store remote additional data for proxy
		ShareItem->GetRemoteAdditionalData(ImplGetData().RemoteAdditionalData);
	}
}

void FTextureShareInstance::UpdateData_RenderThread(const FTextureShareInstanceData& ProxyData)
{
	ImplGetData_RenderThread() = ProxyData;
}

bool FTextureShareInstance::SendSceneContext_RenderThread(FRHICommandListImmediate& RHICmdList, class FSceneRenderTargets& SceneContext, class FSceneViewFamily& ViewFamily)
{
	check(IsInRenderingThread());

	if (ViewFamily.Views.Num() < 1)
	{
		// Now capture from ViewFamily.Views[0]
		return false;
	}

	if (ShareItem.IsValid() && ShareItem->IsValid() && ShareItem->BeginFrame_RenderThread())
	{
		//Get additional information from ViewFamily
		const FSceneView* SceneView = ViewFamily.Views[0];

		// Complete additional frame data
		FTextureShareAdditionalData AdditionalData;
		{
			AdditionalData.FrameNumber = GetDataConstRef_RenderThread().FrameNumber;

			AdditionalData.PrjMatrix = SceneView->ViewMatrices.GetProjectionMatrix();

			AdditionalData.ViewMatrix = SceneView->ViewMatrices.GetViewMatrix();
			AdditionalData.ViewLocation = SceneView->ViewLocation;
			AdditionalData.ViewRotation = SceneView->ViewRotation;

			//@todo: check
			AdditionalData.ViewScale = SceneView->ViewMatrices.GetViewMatrix().GetScaleVector();

		}
		ShareItem->SetLocalAdditionalData(AdditionalData);
	
#if WITH_MGPU
		if (ViewFamily.bMultiGPUForkAndJoin)
		{
			// Setup GPU index for all shared scene textures
			ShareItem->SetDefaultGPUIndex(SceneView->GPUMask.GetFirstIndex());
		}
#endif

		SendTexture_RenderThread(RHICmdList, TextureShareStrings::texture_name::SceneColor, SceneContext.GetSceneColorTexture());

		SendTexture_RenderThread(RHICmdList, TextureShareStrings::texture_name::SceneDepth, SceneContext.SceneDepthZ);
		SendTexture_RenderThread(RHICmdList, TextureShareStrings::texture_name::SmallDepthZ, SceneContext.SmallDepthZ);

		SendTexture_RenderThread(RHICmdList, TextureShareStrings::texture_name::GBufferA, SceneContext.GBufferA);
		SendTexture_RenderThread(RHICmdList, TextureShareStrings::texture_name::GBufferB, SceneContext.GBufferB);
		SendTexture_RenderThread(RHICmdList, TextureShareStrings::texture_name::GBufferC, SceneContext.GBufferC);
		SendTexture_RenderThread(RHICmdList, TextureShareStrings::texture_name::GBufferD, SceneContext.GBufferD);
		SendTexture_RenderThread(RHICmdList, TextureShareStrings::texture_name::GBufferE, SceneContext.GBufferE);
		SendTexture_RenderThread(RHICmdList, TextureShareStrings::texture_name::GBufferF, SceneContext.GBufferF);

		//@todo: add more if needed

		return true;
	}

	return false;
}

bool FTextureShareInstance::SendPostRender_RenderThread(FRHICommandListImmediate& RHICmdList, class FSceneViewFamily& ViewFamily)
{
	check(IsInRenderingThread());

	if (ShareItem.IsValid() && ShareItem->IsValid() && ShareItem->IsLocalFrameLocked())
	{
		if (ViewFamily.RenderTarget && ViewFamily.Views.Num() > 0)
		{
			// Get RTT texture
			FTexture2DRHIRef RenderTargetTexture = ViewFamily.RenderTarget->GetRenderTargetTexture();

			// Send rect of RTT texture
			SendTexture_RenderThread(RHICmdList, TextureShareStrings::texture_name::BackBuffer, RenderTargetTexture.GetReference(), GetDataConstRef_RenderThread().GetRTTRect());
		}

		ShareItem->EndFrame_RenderThread();
		return true;
	}

	return false;
}

bool FTextureShareInstance::RegisterTexture(const FString& InTextureName, const FIntPoint& InSize, EPixelFormat InFormat, ETextureShareSurfaceOp OperationType)
{
	if (ShareItem.IsValid())
	{
		return ShareItem->RegisterTexture(InTextureName.ToLower(), InSize, ETextureShareFormat::Format_EPixel, InFormat, OperationType);
	}

	return false;
}

bool FTextureShareInstance::SendTexture_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& TextureName, const TRefCountPtr<IPooledRenderTarget>& PooledRenderTargetRef)
{
	if (PooledRenderTargetRef.IsValid())
	{
		const FTexture2DRHIRef& RHITexture = (const FTexture2DRHIRef&)PooledRenderTargetRef->GetRenderTargetItem().ShaderResourceTexture;
		return SendTexture_RenderThread(RHICmdList, TextureName, RHITexture.GetReference());
	}
	return false;
}

bool FTextureShareInstance::SendTexture_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& InTextureName, FRHITexture* SrcTexture, const FIntRect* SrcTextureRect)
{
	FString TextureName = InTextureName.ToLower();
	if (ShareItem->IsRemoteTextureUsed(TextureName))
	{
		if (SrcTexture && SrcTexture->IsValid())
		{
			// register size+format, then send texture
			FIntVector InSize = SrcTexture->GetSizeXYZ();
			FIntPoint  SrcRectSize = SrcTextureRect ? SrcTextureRect->Size() : FIntPoint(InSize.X, InSize.Y);

			if (RegisterTexture(TextureName, SrcRectSize, SrcTexture->GetFormat(), ETextureShareSurfaceOp::Write))
			{
				return WriteToShare_RenderThread(RHICmdList, TextureName, SrcTexture, SrcTextureRect);
			}
		}
	}

	return false;
}

bool FTextureShareInstance::ReceiveTexture_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& TextureName, FRHITexture* DstTexture, const FIntRect* DstTextureRect)
{
	if (DstTexture && DstTexture->IsValid())
	{
		// register size+format, then receive texture
		FIntVector InSize = DstTexture->GetSizeXYZ();
		FIntPoint  DstRectSize = DstTextureRect ? DstTextureRect->Size() : FIntPoint(InSize.X, InSize.Y);

		if (RegisterTexture(TextureName, DstRectSize, DstTexture->GetFormat(), ETextureShareSurfaceOp::Read))
		{
			return ReadFromShare_RenderThread(RHICmdList, TextureName, DstTexture, DstTextureRect);
		}
	}

	return false;
}

bool FTextureShareInstance::WriteToShare_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& InTextureName, FRHITexture* SrcTexture, const FIntRect* SrcTextureRect)
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

bool FTextureShareInstance::ReadFromShare_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& InTextureName, FRHITexture* DstTexture, const FIntRect* DstTextureRect)
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
