// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterPostprocessTextureShare.h"

#include "Engine/RendererSettings.h"
#include "Misc/Paths.h"

#include "DisplayClusterPostprocessLog.h"
#include "DisplayClusterPostprocessStrings.h"

#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterStrings.h"

#include "Render/IDisplayClusterRenderManager.h"
#include "Render/Projection/IDisplayClusterProjectionPolicy.h"
#include "Render/Device/IDisplayClusterRenderDevice.h"

#include "ITextureShare.h"
#include "ITextureShareItem.h"
/*

//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterPostProcess
//////////////////////////////////////////////////////////////////////////////////////////////
ITextureShare& TextureShareAPI()
{
	static ITextureShare& SingletonTextureShareApi = ITextureShare::Get();
	return SingletonTextureShareApi;
}

FString GetDisplayClusterViewportShareName(const FString& ShareName)
{
	//@todo: Make unique share name?
	//static constexpr auto DefaultShareName     = TEXT("nDisplay");
	//return FString::Printf(TEXT("%s_%s"), DisplayClusterStrings::cfg::data::postprocess::texture_sharing::DefaultShareName, *ShareName);
	return ShareName;
}

bool FDisplayClusterPostprocessTextureShare::CreateResource(const FString& ShareName, const FDisplayClusterViewport& ResourceViewport, int ResourceViewportIndex, FRHITexture2D* ResourceTexture) const
{
	//@todo: add custom sync setup
	FTextureShareSyncPolicy SyncPolicy;

	//Create internal share (DX12 MGPU)
	if (!TextureShareAPI().CreateShare(ShareName, SyncPolicy, ETextureShareProcess::Server))
	{
		UE_LOG(LogDisplayClusterPostprocessTextureShare, Error, TEXT("Failed create viewport share '%s'"), *ShareName);
		return false;
	}

	{
		FScopeLock lock(&DataGuard);
		ShareResourceNames.AddUnique(ShareName);
	}

	return true;
}

bool FDisplayClusterPostprocessTextureShare::OpenResource(const FString& ShareName) const
{
	//@todo: add custom sync setup
	FTextureShareSyncPolicy SyncPolicy;
	SyncPolicy.TextureSync = ETextureShareSyncSurface::SyncPairingRead; // Viewport client process require external texture
	
	if (!TextureShareAPI().CreateShare(ShareName, SyncPolicy, ETextureShareProcess::Client))
	{
		UE_LOG(LogDisplayClusterPostprocessTextureShare, Error, TEXT("Failed to open share '%s'"), *ShareName);
		return false;
	}

	TSharedPtr<ITextureShareItem> ShareItem;
	if (!TextureShareAPI().GetShare(ShareName, ShareItem) || !ShareItem->RegisterTexture(DisplayClusterPostprocessStrings::texture_share::BackbufferTextureId, FIntPoint(0, 0), ETextureShareFormat::Undefined, 0, ETextureShareSurfaceOp::Read))
	{
		UE_LOG(LogDisplayClusterPostprocessTextureShare, Error, TEXT("Failed to open share '%s' texture"), *ShareName);
		TextureShareAPI().ReleaseShare(ShareName);
		return false;
	}
	
	{
		FScopeLock lock(&DataGuard);
		ShareResourceNames.AddUnique(ShareName);
	}

	return true;
}

bool FDisplayClusterPostprocessTextureShare::BeginSession() const
{
	FScopeLock lock(&DataGuard);

	for (auto& It : ShareResourceNames)
	{
		TSharedPtr<ITextureShareItem> ShareItem;
		if (TextureShareAPI().GetShare(It, ShareItem))
		{
			ShareItem->BeginSession();
		}
	}

	return true;
}

bool FDisplayClusterPostprocessTextureShare::EndSession() const
{
	FScopeLock lock(&DataGuard);

	for (auto& It : ShareResourceNames)
	{
		TextureShareAPI().ReleaseShare(It);
	}

	ShareResourceNames.Empty();

	return true;
}

bool FDisplayClusterPostprocessTextureShare::SendResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* SrcResource, const FIntRect& SrcTextureRect) const
{
	TSharedPtr<ITextureShareItem> ShareItem;
	if (TextureShareAPI().GetShare(ResourceID, ShareItem))
	{
		if (ShareItem->BeginFrame_RenderThread())
		{
			TextureShareAPI().SendTexture_RenderThread(RHICmdList, ShareItem, DisplayClusterPostprocessStrings::texture_share::BackbufferTextureId, SrcResource, &SrcTextureRect);
			ShareItem->EndFrame_RenderThread();
		}
	}

	return false;
}

bool FDisplayClusterPostprocessTextureShare::ReceiveResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* DstResource, const FIntRect& DstTextureRect) const
{
	TSharedPtr<ITextureShareItem> ShareItem;
	if (TextureShareAPI().GetShare(ResourceID, ShareItem))
	{
		if (ShareItem->BeginFrame_RenderThread())
		{
			TextureShareAPI().ReceiveTexture_RenderThread(RHICmdList, ShareItem, DisplayClusterPostprocessStrings::texture_share::BackbufferTextureId, DstResource, &DstTextureRect);
			ShareItem->EndFrame_RenderThread();
		}
	}

	return false;
}

bool FDisplayClusterPostprocessTextureShare::ImplCreateResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FDisplayClusterViewport& ResourceViewport, int ResourceViewportIndex, FRHITexture2D* ResourceTexture) const
{
	FString ShareName = GetDisplayClusterViewportShareName(ResourceViewport.GetId());
	bool bResult = CreateResource_RenderThread(RHICmdList, ShareName, ResourceViewport, ResourceViewportIndex, ResourceTexture);

	//Test: open duplicates
	if (TestDuplicateTextures > 0)
	{
		for (int i = 0; i < TestDuplicateTextures; ++i)
		{
			FString DupShareName = FString::Printf(TEXT("%s_%u"), *ShareName, i);
			CreateResource_RenderThread(RHICmdList, DupShareName, ResourceViewport, ResourceViewportIndex, ResourceTexture);
		}
	}

	return bResult;
}

bool FDisplayClusterPostprocessTextureShare::ImplCreateResource(const FDisplayClusterViewport& ResourceViewport, int ResourceViewportIndex, FRHITexture2D* ResourceTexture) const
{
	FString ShareName = GetDisplayClusterViewportShareName(ResourceViewport.GetId());
	bool bResult = CreateResource(ShareName, ResourceViewport, ResourceViewportIndex, ResourceTexture);

	//Test: open duplicates
	if (TestDuplicateTextures > 0)
	{
		for (int i = 0; i < TestDuplicateTextures; ++i)
		{
			FString DupShareName = FString::Printf(TEXT("%s_%u"), *ShareName, i);
			CreateResource(DupShareName, ResourceViewport, ResourceViewportIndex, ResourceTexture);
		}
	}

	return bResult;
}

bool FDisplayClusterPostprocessTextureShare::ImplOpenResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ShareName) const
{
	bool bResult = OpenResource_RenderThread(RHICmdList, ShareName);

	//Test: open duplicates
	if (TestDuplicateTextures > 0)
	{
		for (int i = 0; i < TestDuplicateTextures; ++i)
		{
			FString DupShareName = FString::Printf(TEXT("%s_%u"), *ShareName, i);
			OpenResource_RenderThread(RHICmdList, DupShareName);
		}
	}

	return bResult;
}
bool FDisplayClusterPostprocessTextureShare::ImplOpenResource(const FString& ShareName) const
{
	bool bResult = OpenResource(ShareName);

	//Test: open duplicates
	if (TestDuplicateTextures > 0)
	{
		for (int i = 0; i < TestDuplicateTextures; ++i)
		{
			FString DupShareName = FString::Printf(TEXT("%s_%u"), *ShareName, i);
			OpenResource(DupShareName);
		}
	}

	return bResult;
}
bool FDisplayClusterPostprocessTextureShare::ImplSendResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* SrcResource, const FIntRect& SrcTextureRect) const
{
	bool bResult = SendResource_RenderThread(RHICmdList, ResourceID, SrcResource, SrcTextureRect);

	//Test: send duplicates
	if (TestDuplicateTextures > 0)
	{
		for (int i = 0; i < TestDuplicateTextures; ++i)
		{
			FString DupShareName = FString::Printf(TEXT("%s_%u"), *ResourceID, i);
			SendResource_RenderThread(RHICmdList, DupShareName, SrcResource, SrcTextureRect);
		}
	}

	return bResult;
}

bool FDisplayClusterPostprocessTextureShare::ImplReceiveResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* DstResource, const FIntRect& DstTextureRect) const
{
	bool bResult = ReceiveResource_RenderThread(RHICmdList, ResourceID, DstResource, DstTextureRect);

	//Test: receive duplicates
	if (TestDuplicateTextures > 0)
	{
		for (int i = 0; i < TestDuplicateTextures; ++i)
		{
			FString DupShareName = FString::Printf(TEXT("%s_%u"), *ResourceID, i);
			ReceiveResource_RenderThread(RHICmdList, DupShareName, DstResource, DstTextureRect);
		}
	}

	return bResult;
}

void FDisplayClusterPostprocessTextureShare::PerformUpdateViewport(const FViewport& MainViewport)
{
	static bool bResourceInitialized = false;
	if (!bResourceInitialized)
	{
		bResourceInitialized = true;
		InitializeResources(MainViewport, RenderViewports);
	}
}

bool FDisplayClusterPostprocessTextureShare::IsPostProcessRenderTargetBeforeWarpBlendRequired()
{
	return bIsEnabled;
}

void FDisplayClusterPostprocessTextureShare::PerformPostProcessRenderTargetBeforeWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* InOutTexture) const
{
	static bool bResourceInitialized = false;
	if (!bResourceInitialized)
	{
		bResourceInitialized = true;
		InitializeResources_RenderThread(RHICmdList, InOutTexture, RenderViewports);
	}

	SendViewports_RenderThread(RHICmdList, InOutTexture, RenderViewports);
	ReceiveViewports_RenderThread(RHICmdList, InOutTexture);
}

void FDisplayClusterPostprocessTextureShare::InitializeResources_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* InOutTexture) const
{
	// Create master share for all render viewports
	for (int CurrentViewportIndex = 0; CurrentViewportIndex < RenderViewports.Num(); CurrentViewportIndex++)
	{
		const FDisplayClusterViewport& CurrentViewport = RenderViewports[CurrentViewportIndex];
		if (ShareViewportsMap.Contains(CurrentViewport.GetId()))
		{
			ImplCreateResource_RenderThread(RHICmdList, CurrentViewport, CurrentViewportIndex, InOutTexture);
		}
	}

	// Open slave share for all render viewports
	for (const auto& It : DestinationViewportsMap)
	{
		ImplOpenResource_RenderThread(RHICmdList, GetDisplayClusterViewportShareName(It.Value));
	}

	BeginSession_RenderThread(RHICmdList);
}


void FDisplayClusterPostprocessTextureShare::InitializeResources(const FViewport& MainViewport)
{
	const FTexture2DRHIRef& Backbuffer = MainViewport.GetRenderTargetTexture();

	// Create master share for all render viewports
	for (int CurrentViewportIndex = 0; CurrentViewportIndex < RenderViewports.Num(); CurrentViewportIndex++)
	{
		const FDisplayClusterViewport& CurrentViewport = RenderViewports[CurrentViewportIndex];
		if (ShareViewportsMap.Contains(CurrentViewport.GetId()))
		{
			ImplCreateResource(CurrentViewport, CurrentViewportIndex, Backbuffer);
		}
	}

	// Open slave share for all render viewports
	for (const auto& It : DestinationViewportsMap)
	{
		ImplOpenResource(GetDisplayClusterViewportShareName(It.Value));
	}

	BeginSession();
}

void FDisplayClusterPostprocessTextureShare::SendViewports_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* InOutTexture) const
{
	// Send all render viewports
	for (const auto& It : RenderViewports)
	{
		if (ShareViewportsMap.Contains(It.GetId()))
		{
			ImplSendResource_RenderThread(RHICmdList, GetDisplayClusterViewportShareName(It.GetId()), InOutTexture, It.GetRect());

			//Test: Send duplicates
			for (int i = 0; i < TestRepeatCopy; ++i)
			{
				ImplSendResource_RenderThread(RHICmdList, GetDisplayClusterViewportShareName(It.GetId()), InOutTexture, It.GetRect());
			}
		}
	}
}

void FDisplayClusterPostprocessTextureShare::ReceiveViewports_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* InOutTexture) const
{
	IDisplayClusterRenderDevice* RenderDevice = IDisplayCluster::Get().GetRenderMgr()->GetRenderDevice();
	if (RenderDevice)
	{
		// Receive to all the viewports defined in the config
		for (const auto& It : DestinationViewportsMap)
		{
			FIntRect ViewortRect;
			if (RenderDevice->GetViewportRect(It.Key, ViewortRect))
			{
				ImplReceiveResource_RenderThread(RHICmdList, GetDisplayClusterViewportShareName(It.Value), InOutTexture, ViewortRect);

				//Test: Send duplicates
				for (int i = 0; i < TestRepeatCopy; ++i)
				{
					ImplReceiveResource_RenderThread(RHICmdList, GetDisplayClusterViewportShareName(It.Value), InOutTexture, ViewortRect);
				}
			}
		}
	}
}

void FDisplayClusterPostprocessTextureShare::Release()
{
	if (bIsEnabled)
	{
		bIsEnabled = false;
		//@todo add release
	}
}

void FDisplayClusterPostprocessTextureShare::InitializePostProcess(class IDisplayClusterViewportManager& InViewportManager, const TMap<FString, FString>& Parameters)
{
	TArray<FString> CfgShareViewports;
	if(DisplayClusterHelpers::map::template ExtractArrayFromString(Parameters, DisplayClusterPostprocessStrings::texture_share::ShareViewports, CfgShareViewports, DisplayClusterStrings::common::ArrayValSeparator))
	{
		FString DbgLog;
		for (auto& It : CfgShareViewports)
		{
			ShareViewportsMap.Add(It.ToLower(), true);
			bIsEnabled = true;
			DbgLog.Append(It);
			DbgLog.Append(FString(DisplayClusterStrings::common::ArrayValSeparator));
		}

		UE_LOG(LogDisplayClusterPostprocessTextureShare, Log, TEXT("Found Argument '%s'='%s'"), DisplayClusterPostprocessStrings::texture_share::ShareViewports, *DbgLog);
	}

	TArray<FString> CfgSourceTextures;
	if (DisplayClusterHelpers::map::template ExtractArrayFromString(Parameters, DisplayClusterPostprocessStrings::texture_share::SourceShare, CfgShareViewports, DisplayClusterStrings::common::ArrayValSeparator))
	{
		FString DbgLog;
		for (auto& It : CfgSourceTextures)
		{
			DbgLog.Append(It);
			DbgLog.Append(FString(DisplayClusterStrings::common::ArrayValSeparator));
		}

		UE_LOG(LogDisplayClusterPostprocessTextureShare, Log, TEXT("Found Argument '%s'='%s'"), DisplayClusterPostprocessStrings::texture_share::SourceShare, *DbgLog);
	}

	TArray<FString> CfgDestinationViewports;
	if (DisplayClusterHelpers::map::template ExtractArrayFromString(Parameters, DisplayClusterPostprocessStrings::texture_share::DestinationViewports, CfgShareViewports, DisplayClusterStrings::common::ArrayValSeparator))
	{
		FString DbgLog;
		for (auto& It : CfgDestinationViewports)
		{
			DbgLog.Append(It);
			DbgLog.Append(FString(DisplayClusterStrings::common::ArrayValSeparator));
		}

		UE_LOG(LogDisplayClusterPostprocessTextureShare, Log, TEXT("Found Argument '%s'='%s'"), DisplayClusterPostprocessStrings::texture_share::DestinationViewports, *DbgLog);
	}

	if (CfgSourceTextures.Num() != CfgDestinationViewports.Num())
	{
		UE_LOG(LogDisplayClusterPostprocessTextureShare, Error, TEXT("Argument count in '%s' and '%s' must be equal"), DisplayClusterPostprocessStrings::texture_share::SourceShare, DisplayClusterPostprocessStrings::texture_share::DestinationViewports);
		return;
	}

	// Create viewports to texture map
	for (int i = 0;i < CfgSourceTextures.Num();i++)
	{
		DestinationViewportsMap.Add(CfgDestinationViewports[i].ToLower(), CfgSourceTextures[i].ToLower());
		bIsEnabled = true;
	}

	// Get test params
	if (DisplayClusterHelpers::map::template ExtractValueFromString(Parameters, DisplayClusterPostprocessStrings::texture_share::debug::DuplicateTexture, TestDuplicateTextures))
	{
		UE_LOG(LogDisplayClusterPostprocessTextureShare, Log, TEXT("Found Argument '%s'=%u"), DisplayClusterPostprocessStrings::texture_share::debug::DuplicateTexture, TestDuplicateTextures);
	}

	if (DisplayClusterHelpers::map::template ExtractValueFromString(Parameters, DisplayClusterPostprocessStrings::texture_share::debug::RepeatCopy, TestRepeatCopy))
	{
		UE_LOG(LogDisplayClusterPostprocessTextureShare, Log, TEXT("Found Argument '%s'=%u"), DisplayClusterPostprocessStrings::texture_share::debug::RepeatCopy, TestRepeatCopy);
	}
}
*/
