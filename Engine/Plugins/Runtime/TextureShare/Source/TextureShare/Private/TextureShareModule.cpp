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

//////////////////////////////////////////////////////////////////////////////////////////////
static bool ImplFindTextureShare(bool bIsInRenderingThread, const TArray<TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>>& InTextureShares, const FString& ShareName, TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& OutTextureShare)
{
	TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe> const* DesiredShare = InTextureShares.FindByPredicate([ShareName](const TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& ShareIt)
	{
		return ShareIt.IsValid() && ShareIt->Equals(ShareName);
	});

	if (DesiredShare)
	{
		OutTextureShare = *DesiredShare;
		return true;
	}

	return false;
}

static bool ImplFindTextureShare(bool bIsInRenderingThread, const TArray<TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>>& InTextureShares, const TSharedPtr<ITextureShareItem>& ShareItem, TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& OutTextureShare)
{
	if (ShareItem.IsValid() && ShareItem->IsValid())
	{
		TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe> const* DesiredShare = InTextureShares.FindByPredicate([ShareItem](const TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& ShareIt)
		{
			return ShareIt.IsValid() && ShareIt->Equals(ShareItem);
		});

		if (DesiredShare)
		{
			OutTextureShare = *DesiredShare;
			return true;
		}
	}

	return false;
}

static bool ImplFindTextureShare(bool bIsInRenderingThread, const TArray<TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>>& InTextureShares, int32 InStereoscopicPass, TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& OutTextureShare)
{
	TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe> const* DesiredShare = InTextureShares.FindByPredicate([bIsInRenderingThread, InStereoscopicPass](const TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& ShareIt)
	{
		if (ShareIt.IsValid())
		{
			const FTextureShareInstanceData& ShareData = bIsInRenderingThread ? ShareIt->GetDataConstRef_RenderThread() : ShareIt->GetDataConstRef();
			return ShareData.StereoscopicPass == InStereoscopicPass;
		}

		return false;
	});

	if (DesiredShare)
	{
		OutTextureShare = *DesiredShare;
		return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Impl RenderThread
//////////////////////////////////////////////////////////////////////////////////////////////
static void ImplCreateTextureShareProxy_RenderThread(TArray<TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>>& InOutTextureSharesProxy, const TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& InShareInstance)
{
	InOutTextureSharesProxy.Add(InShareInstance);
}

static void ImplReleaseTextureShareProxy_RenderThread(TArray<TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>>& InOutTextureSharesProxy, const FString& ReleasedShareName)
{
	for (int32 Index = 0; Index < InOutTextureSharesProxy.Num(); Index++)
	{
		const TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& Instance = InOutTextureSharesProxy[Index];
		if(Instance.IsValid() && Instance->Equals(ReleasedShareName))
		{
			InOutTextureSharesProxy[Index].Reset();
			InOutTextureSharesProxy.RemoveAt(Index);
			return;
		}
	}
}

static void ImplUpdateTextureSharesProxy_RenderThread(TArray<TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>>& InOutTextureSharesProxy, TMap<FString, FTextureShareInstanceData>& InNewProxyData)
{
	// Update exist and collect removed proxy:
	TArray<TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>> ReleasedProxy;
	for (TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& It : InOutTextureSharesProxy)
	{
		FTextureShareInstanceData* const NewData = InNewProxyData.Find(It->ShareName);

		if (NewData)
		{
			It->UpdateData_RenderThread(*NewData);
		}
		else
		{
			ReleasedProxy.Add(It);
		}
	}

	// Release
	for (TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& TextureShareInstanceIt : ReleasedProxy)
	{
		int32 InstanceIndex = InOutTextureSharesProxy.Find(TextureShareInstanceIt);
		TextureShareInstanceIt.Reset();

		if (InstanceIndex != INDEX_NONE)
		{
			InOutTextureSharesProxy[InstanceIndex].Reset();
			InOutTextureSharesProxy.RemoveAt(InstanceIndex);
		}
	}
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
	UE_LOG(LogTextureShare, Log, TEXT("TextureShare module shutdown"));
}

//////////////////////////////////////////////////////////////////////////////////////////////
// FTextureShareModule
//////////////////////////////////////////////////////////////////////////////////////////////
FTextureShareModule::FTextureShareModule()
{
	DisplayManager = MakeShareable(new FTextureShareDisplayManager(*this));
	UE_LOG(LogTextureShare, Log, TEXT("TextureShare module has been instantiated"));
}

FTextureShareModule::~FTextureShareModule()
{
	UE_LOG(LogTextureShare, Log, TEXT("TextureShare module has been destroyed"));
}

bool FTextureShareModule::LinkSceneContextToShare(const TSharedPtr<ITextureShareItem>& ShareItem, int StereoscopicPass, bool bIsEnabled)
{
	check(IsInGameThread());

	TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe> TextureShareInstance;
	if (FindTextureShare(ShareItem, TextureShareInstance))
	{
		TextureShareInstance->LinkSceneContext(bIsEnabled ? StereoscopicPass : -1, DisplayManager);
		return true;
	}

	// Share not exist
	return false;
}

bool FTextureShareModule::SetBackbufferRect(int StereoscopicPass, const FIntRect* BackbufferRect)
{
	check(IsInGameThread());

	TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe> TextureShareInstance;
	if (FindTextureShare(StereoscopicPass, TextureShareInstance))
	{
		TextureShareInstance->SetRTTRect(BackbufferRect);
		return true;
	}

	// Share not exist
	return false;
}

bool FTextureShareModule::CreateShare(const FString& ShareName, const FTextureShareSyncPolicy& SyncMode, ETextureShareProcess Process, float SyncWaitTime)
{
	check(IsInGameThread());

	TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe> NewInstance;
	if (FTextureShareInstance::Create(NewInstance, ShareName, SyncMode, Process))
	{
		TextureShares.Add(NewInstance);

		ENQUEUE_RENDER_COMMAND(CreateTextureSharesProxy)(
			[TextureShareModule = this, NewProxyInstance = NewInstance](FRHICommandListImmediate& RHICmdList)
		{
			ImplCreateTextureShareProxy_RenderThread(TextureShareModule->TextureSharesProxy, NewProxyInstance);
		});

		return true;
	}

	return false;
}

bool FTextureShareModule::ReleaseShare(const FString& ShareName)
{
	check(IsInGameThread());

	TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe> TextureShareInstance;
	if (FindTextureShare(ShareName, TextureShareInstance))
	{
		int32 InstanceIndex = TextureShares.Find(TextureShareInstance);
		TextureShareInstance.Reset();

		if (InstanceIndex != INDEX_NONE)
		{
			TextureShares[InstanceIndex].Reset();
			TextureShares.RemoveAt(InstanceIndex);
		}

		FString ReleasedShareName = ShareName;
		ENQUEUE_RENDER_COMMAND(CreateTextureSharesProxy)(
			[TextureShareModule = this, ReleasedShareName](FRHICommandListImmediate& RHICmdList)
		{
			ImplReleaseTextureShareProxy_RenderThread(TextureShareModule->TextureSharesProxy, ReleasedShareName);
		});

		return true;
	}

	return false;
}

bool FTextureShareModule::GetShare(const FString& ShareName, TSharedPtr<ITextureShareItem>& OutShareItem) const
{
	TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe> TextureShareInstance;
	if (IsInGameThread()?FindTextureShare(ShareName, TextureShareInstance): FindTextureShare_RenderThread(ShareName, TextureShareInstance))
	{
		OutShareItem = TextureShareInstance->GetTextureShareItem();
		return true;
	}

	return false;
}

void FTextureShareModule::UpdateTextureSharesProxy()
{
	check(IsInGameThread());

	// Get new proxy data updates
	TMap<FString, FTextureShareInstanceData>* NewProxyData = new TMap<FString, FTextureShareInstanceData>();
	for (TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& It : TextureShares)
	{
		if (It->IsValid())
		{
			NewProxyData->Add(It->ShareName, It->GetDataConstRef());
		}
	}

	// send updates to renderthread
	ENQUEUE_RENDER_COMMAND(UpdateTextureSharesProxy)(
		[TextureShareModule = this, NewProxyData = NewProxyData](FRHICommandListImmediate& RHICmdList)
	{
		ImplUpdateTextureSharesProxy_RenderThread(TextureShareModule->TextureSharesProxy, *NewProxyData);
		delete NewProxyData;
	});
}

void FTextureShareModule::OnBeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	check(IsInGameThread());

	if (InViewFamily.Views.Num() > 0)
	{
		EStereoscopicPass StereoscopicPass = InViewFamily.Views[0]->StereoPass;

		TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe> TextureShareInstance;
		if (FindTextureShare(StereoscopicPass, TextureShareInstance))
		{
			TextureShareInstance->HandleBeginNewFrame();
		}
	}

	// Always update texture shares proxy list before viewfamily render
	UpdateTextureSharesProxy();
}

void FTextureShareModule::OnResolvedSceneColor_RenderThread(FRHICommandListImmediate& RHICmdList, class FSceneRenderTargets& SceneContext, class FSceneViewFamily& ViewFamily)
{
	check(IsInRenderingThread());

	if (ViewFamily.Views.Num() > 0)
	{
		EStereoscopicPass StereoscopicPass = ViewFamily.Views[0]->StereoPass;

		TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe> TextureShareInstance;
		if (FindTextureShare_RenderThread(StereoscopicPass, TextureShareInstance))
		{
			TextureShareInstance->SendSceneContext_RenderThread(RHICmdList, SceneContext, ViewFamily);
		}
	}
}

void FTextureShareModule::OnPostRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, class FSceneViewFamily& ViewFamily)
{
	check(IsInRenderingThread());

	if (ViewFamily.Views.Num() > 0)
	{
		EStereoscopicPass StereoscopicPass = ViewFamily.Views[0]->StereoPass;

		TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe> TextureShareInstance;
		if (FindTextureShare_RenderThread(StereoscopicPass, TextureShareInstance))
		{
			TextureShareInstance->SendPostRender_RenderThread(RHICmdList, ViewFamily);
		}
	}
}

bool FTextureShareModule::RegisterTexture(const TSharedPtr<ITextureShareItem>& ShareItem, const FString& InTextureName, const FIntPoint& InSize, EPixelFormat InFormat, ETextureShareSurfaceOp OperationType)
{
	TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe> TextureShareInstance;
	bool bIsExist = IsInGameThread() ? FindTextureShare(ShareItem, TextureShareInstance) : FindTextureShare_RenderThread(ShareItem, TextureShareInstance);
	if(bIsExist)
	{
		return TextureShareInstance->RegisterTexture(InTextureName, InSize, InFormat, OperationType);
	}

	return false;
}

bool FTextureShareModule::SendTexture_RenderThread(FRHICommandListImmediate& RHICmdList, const TSharedPtr<ITextureShareItem>& ShareItem, const FString& InTextureName, FRHITexture* RHITexture, const FIntRect* SrcTextureRect)
{
	check(IsInRenderingThread());

	TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe> TextureShareInstance;
	if (FindTextureShare_RenderThread(ShareItem, TextureShareInstance))
	{
		return TextureShareInstance->SendTexture_RenderThread(RHICmdList, InTextureName, RHITexture, SrcTextureRect);
	}

	return false;
}

bool FTextureShareModule::WriteToShare_RenderThread(FRHICommandListImmediate& RHICmdList, const TSharedPtr<ITextureShareItem>& ShareItem, const FString& InTextureName, FRHITexture* SrcTexture, const FIntRect* SrcTextureRect)
{
	check(IsInRenderingThread());

	TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe> TextureShareInstance;
	if (FindTextureShare_RenderThread(ShareItem, TextureShareInstance))
	{
		TextureShareInstance->WriteToShare_RenderThread(RHICmdList, InTextureName, SrcTexture, SrcTextureRect);
	}

	return false;
}

bool FTextureShareModule::ReceiveTexture_RenderThread(FRHICommandListImmediate& RHICmdList, const TSharedPtr<ITextureShareItem>& ShareItem, const FString& TextureName, FRHITexture* RHITexture, const FIntRect* DstTextureRect)
{
	check(IsInRenderingThread());

	TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe> TextureShareInstance;
	if (FindTextureShare_RenderThread(ShareItem, TextureShareInstance))
	{
		TextureShareInstance->ReceiveTexture_RenderThread(RHICmdList, TextureName, RHITexture, DstTextureRect);
	}

	return false;
}

bool FTextureShareModule::ReadFromShare_RenderThread(FRHICommandListImmediate& RHICmdList, const TSharedPtr<ITextureShareItem>& ShareItem, const FString& InTextureName, FRHITexture* DstTexture, const FIntRect* DstTextureRect)
{
	check(IsInRenderingThread());

	TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe> TextureShareInstance;
	if (FindTextureShare_RenderThread(ShareItem, TextureShareInstance))
	{
		TextureShareInstance->ReadFromShare_RenderThread(RHICmdList, InTextureName, DstTexture, DstTextureRect);
	}

	return false;
}

void FTextureShareModule::CastTextureShareBPSyncPolicy(const FTextureShareBPSyncPolicy& InSyncPolicy, FTextureShareSyncPolicy& OutSyncPolicy)
{
	OutSyncPolicy = *InSyncPolicy;
}

bool FTextureShareModule::FindTextureShare(const FString& ShareName, TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& OutTextureShare) const
{
	check(IsInGameThread());

	return ImplFindTextureShare(false, TextureShares, ShareName, OutTextureShare);
}

bool FTextureShareModule::FindTextureShare(const TSharedPtr<ITextureShareItem>& ShareItem, TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& OutTextureShare) const
{
	check(IsInGameThread());

	return ImplFindTextureShare(false, TextureShares, ShareItem, OutTextureShare);
}

bool FTextureShareModule::FindTextureShare(int32 InStereoscopicPass, TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& OutTextureShare) const
{
	check(IsInGameThread());

	return ImplFindTextureShare(false, TextureShares, InStereoscopicPass, OutTextureShare);
}

bool FTextureShareModule::FindTextureShare_RenderThread(const FString& ShareName, TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& OutTextureShare) const
{
	check(IsInRenderingThread());

	return ImplFindTextureShare(true, TextureSharesProxy, ShareName, OutTextureShare);
}

bool FTextureShareModule::FindTextureShare_RenderThread(const TSharedPtr<ITextureShareItem>& ShareItem, TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& OutTextureShare) const
{
	check(IsInRenderingThread());

	return ImplFindTextureShare(true, TextureSharesProxy, ShareItem, OutTextureShare);
}

bool FTextureShareModule::FindTextureShare_RenderThread(int32 InStereoscopicPass, TSharedPtr<FTextureShareInstance, ESPMode::ThreadSafe>& OutTextureShare) const
{
	check(IsInRenderingThread());

	return ImplFindTextureShare(true, TextureSharesProxy, InStereoscopicPass, OutTextureShare);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// ITextureShare
//////////////////////////////////////////////////////////////////////////////////////////////
IMPLEMENT_MODULE(FTextureShareModule, TextureShare);
