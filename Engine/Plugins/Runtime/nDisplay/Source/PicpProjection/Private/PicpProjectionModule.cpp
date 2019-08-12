// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PicpProjectionModule.h"

#include "PicpProjectionLog.h"

#include "Policy/MPCDI/PicpProjectionMPCDIPolicyFactory.h"

#include "IDisplayCluster.h"
#include "Render/IDisplayClusterRenderManager.h"

#include "Policy/MPCDI/PicpProjectionMPCDIPolicy.h"
#include "Policy/MPCDI/PicpProjectionMPCDIPolicyFactory.h"
#include "Engine/TextureRenderTarget2D.h"


FPicpProjectionModule::FPicpProjectionModule()
{
	TSharedPtr<IDisplayClusterProjectionPolicyFactory> Factory;

	// MPCDI projection
	Factory = MakeShareable(new FPicpProjectionMPCDIPolicyFactory);
	ProjectionPolicyFactories.Emplace(PicpProjectionStrings::projection::PicpMPCDI, Factory);

	UE_LOG(LogPicpProjection, Log, TEXT("Projection module has been instantiated"));
}

FPicpProjectionModule::~FPicpProjectionModule()
{
	UE_LOG(LogPicpProjection, Log, TEXT("Projection module has been destroyed"));
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IModuleInterface
//////////////////////////////////////////////////////////////////////////////////////////////
void FPicpProjectionModule::StartupModule()
{
	UE_LOG(LogPicpProjection, Log, TEXT("Projection module startup"));

	IDisplayClusterRenderManager* RenderMgr = IDisplayCluster::Get().GetRenderMgr();
	if (RenderMgr)
	{
		for (auto it = ProjectionPolicyFactories.CreateIterator(); it; ++it)
		{
			UE_LOG(LogPicpProjection, Log, TEXT("Registering <%s> projection policy factory..."), *it->Key);

			if (!RenderMgr->RegisterProjectionPolicyFactory(it->Key, it->Value))
			{
				UE_LOG(LogPicpProjection, Warning, TEXT("Couldn't register <%s> projection policy factory"), *it->Key);
			}
		}
	}

	UE_LOG(LogPicpProjection, Log, TEXT("Projection module has started"));
}

void FPicpProjectionModule::ShutdownModule()
{
	UE_LOG(LogPicpProjection, Log, TEXT("Projection module shutdown"));

	IDisplayClusterRenderManager* RenderMgr = IDisplayCluster::Get().GetRenderMgr();
	if (RenderMgr)
	{
		for (auto it = ProjectionPolicyFactories.CreateConstIterator(); it; ++it)
		{
			UE_LOG(LogPicpProjection, Log, TEXT("Un-registering <%s> projection factory..."), *it->Key);

			if (!RenderMgr->UnregisterProjectionPolicyFactory(it->Key))
			{
				UE_LOG(LogPicpProjection, Warning, TEXT("An error occurred during un-registering the <%s> projection factory"), *it->Key);
			}
		}
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjection
//////////////////////////////////////////////////////////////////////////////////////////////
void FPicpProjectionModule::GetSupportedProjectionTypes(TArray<FString>& OutProjectionTypes)
{
	ProjectionPolicyFactories.GenerateKeyArray(OutProjectionTypes);
}

TSharedPtr<IDisplayClusterProjectionPolicyFactory> FPicpProjectionModule::GetProjectionFactory(const FString& ProjectionType)
{
	if (ProjectionPolicyFactories.Contains(ProjectionType))
	{
		return ProjectionPolicyFactories[ProjectionType];
	}

	UE_LOG(LogPicpProjection, Warning, TEXT("No <%s> projection factory available"), *ProjectionType);

	return nullptr;
}

void FPicpProjectionModule::SetOverlayFrameData(const FString& PolicyType, FPicpProjectionOverlayFrameData& OverlayFrameData)
{
	TArray<TSharedPtr<FPicpProjectionViewportBase>> Result;
	TSharedPtr<IDisplayClusterProjectionPolicyFactory> Factory = GetProjectionFactory(PolicyType);
	if (Factory.IsValid())
	{
		FPicpProjectionMPCDIPolicyFactory* PicpMPCDIFactory = static_cast<FPicpProjectionMPCDIPolicyFactory*>(Factory.Get());
		if (PicpMPCDIFactory)
		{
			TArray<TSharedPtr<IDisplayClusterProjectionPolicy>> UsedPolicy = PicpMPCDIFactory->GetMPCDIPolicy();
			for (auto It : UsedPolicy)
			{
				if (It.IsValid())
				{
					FPicpProjectionMPCDIPolicy* PicpMPCDIPolicy = static_cast<FPicpProjectionMPCDIPolicy*>(It.Get());
					if (PicpMPCDIPolicy != nullptr)
					{
						PicpMPCDIPolicy->UpdateOverlayViewportData(OverlayFrameData);
					}
				}
			}
		}
	}
}

int FPicpProjectionModule::GetPolicyCount(const FString& InProjectionType)
{
	TSharedPtr<IDisplayClusterProjectionPolicyFactory> Factory = GetProjectionFactory(InProjectionType);
	if (Factory.IsValid())
	{
		FPicpProjectionMPCDIPolicyFactory* MPCDIFactory = static_cast<FPicpProjectionMPCDIPolicyFactory*>(Factory.Get());
		if (MPCDIFactory)
		{
			TArray<TSharedPtr<IDisplayClusterProjectionPolicy>> UsedPolicy = MPCDIFactory->GetMPCDIPolicy();
			return UsedPolicy.Num();
		}
	}
	return 0;
}


void FPicpProjectionModule::AddProjectionDataListener(TScriptInterface<IPicpProjectionFrustumDataListener> listener)
{
	PicpEventListeners.Add(listener);
}

void FPicpProjectionModule::RemoveProjectionDataListener(TScriptInterface<IPicpProjectionFrustumDataListener> listener)
{
	PicpEventListeners.Remove(listener);
}

void FPicpProjectionModule::CleanProjectionDataListeners() 
{
	PicpEventListeners.Empty();
}

void FPicpProjectionModule::CaptureWarpTexture(UTextureRenderTarget2D* dst, const FString& ViewportId, const uint32 ViewIdx, bool bCaptureNow)
{
	TSharedPtr<IDisplayClusterProjectionPolicyFactory> Factory = GetProjectionFactory(PicpProjectionStrings::projection::PicpMPCDI);
	if (Factory.IsValid())
	{
		FPicpProjectionMPCDIPolicyFactory* MPCDIFactory = static_cast<FPicpProjectionMPCDIPolicyFactory*>(Factory.Get());
		if (MPCDIFactory)
		{
			TArray<TSharedPtr<IDisplayClusterProjectionPolicy>> UsedPolicy = MPCDIFactory->GetMPCDIPolicy();
			for (auto It : UsedPolicy)
			{
				if (It.IsValid())
				{
					FPicpProjectionMPCDIPolicy* MPCDIPolicy = static_cast<FPicpProjectionMPCDIPolicy*>(It.Get());
					if (MPCDIPolicy != nullptr)
					{
						if (MPCDIPolicy->GetViewportId().Compare(ViewportId, ESearchCase::IgnoreCase) == 0)
						{
							if (bCaptureNow)
							{
								FTextureRenderTarget2DResource* dstResource2D = (FTextureRenderTarget2DResource*)(dst->GameThread_GetRenderTargetResource());
								FRHITexture2D* dstTextureRHI = dstResource2D->GetTextureRHI();

								MPCDIPolicy->SetWarpTextureCapture(ViewIdx, dstTextureRHI);
							}
							else
							{
								MPCDIPolicy->SetWarpTextureCapture(ViewIdx, 0);
							}
						}
					}
				}
			}
		}
	}
}

bool FPicpProjectionModule::GetWarpFrustum(const FString& ViewportId, const uint32 ViewIdx, IMPCDI::FFrustum& OutFrustum, bool bIsCaptureWarpTextureFrustum)
{
	TSharedPtr<IDisplayClusterProjectionPolicyFactory> Factory = GetProjectionFactory(PicpProjectionStrings::projection::PicpMPCDI);
	if (Factory.IsValid())
	{
		FPicpProjectionMPCDIPolicyFactory* MPCDIFactory = static_cast<FPicpProjectionMPCDIPolicyFactory*>(Factory.Get());
		if (MPCDIFactory)
		{
			TArray<TSharedPtr<IDisplayClusterProjectionPolicy>> UsedPolicy = MPCDIFactory->GetMPCDIPolicy();
			for (auto It : UsedPolicy)
			{
				if (It.IsValid())
				{
					FPicpProjectionMPCDIPolicy* MPCDIPolicy = static_cast<FPicpProjectionMPCDIPolicy*>(It.Get());
					if (MPCDIPolicy != nullptr)
					{
						if (MPCDIPolicy->GetViewportId().Compare(ViewportId, ESearchCase::IgnoreCase) == 0)
						{
							OutFrustum = MPCDIPolicy->GetWarpFrustum(ViewIdx, bIsCaptureWarpTextureFrustum);
							return true;
						}
					}
				}
			}
		}
	}
	return false;
}

void FPicpProjectionModule::SetViewport(const FString& Id, FRotator& OutViewRotation, FVector& OutViewLocation, FMatrix& OutPrjMatrix)
{
	TArray<TScriptInterface<IPicpProjectionFrustumDataListener>> EmptyListeners;

	for (auto Listener : PicpEventListeners)
	{
		FPicpProjectionFrustumData data;
		data.Id = Id;
		data.ViewRotation = OutViewRotation;
		data.ViewLocation = OutViewLocation;
		data.PrjMatrix = OutPrjMatrix;

		if (Listener.GetObject() != nullptr && !Listener.GetObject()->IsPendingKill())
		{
			Listener->Execute_OnProjectionDataUpdate(Listener.GetObject(), data);
		}		
		else
		{
			EmptyListeners.Add(Listener);
		}
	}

	for (auto EmptyListener : EmptyListeners)
	{
		PicpEventListeners.Remove(EmptyListener);
	}
}

IMPLEMENT_MODULE(FPicpProjectionModule, PicpProjection)
