// Copyright Epic Games, Inc. All Rights Reserved.

#include "PicpProjectionModule.h"
#include "PicpProjectionLog.h"

#include "IDisplayCluster.h"
#include "Render/IDisplayClusterRenderManager.h"

#include "Policy/MPCDI/PicpProjectionMPCDIPolicy.h"
#include "Policy/MPCDI/PicpProjectionMPCDIPolicyFactory.h"

#include "Policy/Mesh/PicpProjectionMeshPolicy.h"

#include "Engine/TextureRenderTarget2D.h"


FPicpProjectionModule::FPicpProjectionModule()
{
	TSharedPtr<IDisplayClusterProjectionPolicyFactory> Factory;

	// Picp_MPCDI + Picp_MESH projections:
	Factory = MakeShareable(new FPicpProjectionMPCDIPolicyFactory);
	ProjectionPolicyFactories.Emplace(PicpProjectionStrings::projection::PicpMPCDI, Factory);
	ProjectionPolicyFactories.Emplace(PicpProjectionStrings::projection::PicpMesh,  Factory); // Add overried projection for mesh

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
	TSharedPtr<IDisplayClusterProjectionPolicyFactory> Factory = GetProjectionFactory(PolicyType);
	if (Factory.IsValid())
	{
		FPicpProjectionMPCDIPolicyFactory* PicpMPCDIFactory = static_cast<FPicpProjectionMPCDIPolicyFactory*>(Factory.Get());
		if (PicpMPCDIFactory)
		{
			TArray<TSharedPtr<FPicpProjectionPolicyBase>> UsedPolicy = PicpMPCDIFactory->GetPicpPolicy();
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

bool FPicpProjectionModule::AssignWarpMeshToViewport(const FString& ViewportId, UStaticMeshComponent* MeshComponent, USceneComponent* OriginComponent)
{
	TSharedPtr<IDisplayClusterProjectionPolicyFactory> Factory = GetProjectionFactory(PicpProjectionStrings::projection::PicpMesh);
	if (Factory.IsValid())
	{
		FPicpProjectionMPCDIPolicyFactory* PicpMPCDIFactory = static_cast<FPicpProjectionMPCDIPolicyFactory*>(Factory.Get());
		if (PicpMPCDIFactory)
		{
			TSharedPtr<FPicpProjectionPolicyBase> ViewportPolicy = PicpMPCDIFactory->GetPicpPolicyByViewport(ViewportId);
			if (ViewportPolicy.IsValid())
			{
				FPicpProjectionMeshPolicy* PicpMeshPolicy = static_cast<FPicpProjectionMeshPolicy*>(ViewportPolicy.Get());
				if (PicpMeshPolicy != nullptr)
				{
					if (PicpMeshPolicy->GetWarpType() == FPicpProjectionMPCDIPolicy::EWarpType::Mesh)
					{
						return PicpMeshPolicy->AssignWarpMesh(MeshComponent, OriginComponent);
					}
				}
			}
		}
	}

	UE_LOG(LogPicpProjection, Error, TEXT("Viewport '%s' with 'picp_mesh' projection not found"), *ViewportId);
	return false;
}

int FPicpProjectionModule::GetPolicyCount(const FString& InProjectionType)
{
	TSharedPtr<IDisplayClusterProjectionPolicyFactory> Factory = GetProjectionFactory(InProjectionType);
	if (Factory.IsValid())
	{
		FPicpProjectionMPCDIPolicyFactory* PicpMPCDIFactory = static_cast<FPicpProjectionMPCDIPolicyFactory*>(Factory.Get());
		if (PicpMPCDIFactory)
		{
			TArray<TSharedPtr<FPicpProjectionPolicyBase>> UsedPolicy = PicpMPCDIFactory->GetPicpPolicy();
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

void FPicpProjectionModule::CaptureWarpTexture(UTextureRenderTarget2D* OutWarpRTT, const FString& ViewportId, const uint32 ViewIdx, bool bCaptureNow)
{
	TSharedPtr<IDisplayClusterProjectionPolicyFactory> Factory = GetProjectionFactory(PicpProjectionStrings::projection::PicpMPCDI);
	if (Factory.IsValid())
	{
		FPicpProjectionMPCDIPolicyFactory* MPCDIFactory = static_cast<FPicpProjectionMPCDIPolicyFactory*>(Factory.Get());
		if (MPCDIFactory)
		{
			TArray<TSharedPtr<FPicpProjectionPolicyBase>> UsedPolicy = MPCDIFactory->GetPicpPolicy();
			for (auto It : UsedPolicy)
			{
				if (It.IsValid())
				{
					FPicpProjectionMPCDIPolicy* MPCDIPolicy = static_cast<FPicpProjectionMPCDIPolicy*>(It.Get());
					if (MPCDIPolicy != nullptr)
					{
						if (MPCDIPolicy->GetViewportId().Compare(ViewportId, ESearchCase::IgnoreCase) == 0)
						{
							FRHITexture2D* WarpTextureRHI = nullptr;

							if (bCaptureNow && OutWarpRTT)
							{
								FTextureRenderTarget2DResource* OutWarpResource2D = (FTextureRenderTarget2DResource*)(OutWarpRTT->GameThread_GetRenderTargetResource());
								WarpTextureRHI = OutWarpResource2D?(OutWarpResource2D->GetTextureRHI()):nullptr;
							}

							MPCDIPolicy->SetWarpTextureCapture(ViewIdx, WarpTextureRHI);
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
			TArray<TSharedPtr<FPicpProjectionPolicyBase>> UsedPolicy = MPCDIFactory->GetPicpPolicy();
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
