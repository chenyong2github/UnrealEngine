// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/MPCDI/DisplayClusterProjectionMPCDIPolicy.h"
#include "DisplayClusterConfigurationTypes.h"

#include "IMPCDI.h"

#include "Misc/DisplayClusterHelpers.h"
#include "Misc/Paths.h"

#if WITH_EDITOR
#include "Components/DisplayClusterSceneComponent.h"
#include "IDisplayClusterRendering.h"
#include "ProceduralMeshComponent.h"
#endif


UDisplayClusterProjectionPolicyMPCDIParameters::UDisplayClusterProjectionPolicyMPCDIParameters()
	: Super()
#if WITH_EDITOR
	, RootActor(nullptr)
#endif
{
}

#if WITH_EDITOR
bool UDisplayClusterProjectionPolicyMPCDIParameters::Parse(ADisplayClusterRootActor* InRootActor, const FDisplayClusterConfigurationProjection& ConfigData)
{
	FScopeLock Lock(&InternalsSyncScope);

	if (RootActor)
	{
		return false;
	}

	RootActor = InRootActor;

	IMPCDI& MPCDI = IMPCDI::Get();

	IMPCDI::ConfigParser CfgData;

	if (!MPCDI.LoadConfig(ConfigData.Parameters, CfgData))
	{
		return false;
	}

	if (FPaths::IsRelative(CfgData.MPCDIFileName))
	{
		CfgData.MPCDIFileName = DisplayClusterHelpers::filesystem::GetFullPathForConfigResource(CfgData.MPCDIFileName);
	}

	// Load MPCDI config
	if (!MPCDI.Load(CfgData, MPCDILocator))
	{
		return false;
	}

	File   = CfgData.MPCDIFileName;
	Buffer = CfgData.BufferId;
	Region = CfgData.RegionId;
	Origin = Cast<USceneComponent>(RootActor->GetComponentById(CfgData.OriginType));

	if (!Origin)
	{
		Origin = RootActor->GetRootComponent();
	}

	return true;
}


UDisplayClusterProjectionPolicyParameters* FDisplayClusterProjectionMPCDIPolicy::CreateParametersObject(UObject* Owner)
{
	return NewObject<UDisplayClusterProjectionPolicyMPCDIParameters>(Owner);
}

void FDisplayClusterProjectionMPCDIPolicy::InitializePreview(UDisplayClusterProjectionPolicyParameters* PolicyParameters)
{
	UDisplayClusterProjectionPolicyMPCDIParameters* MPCDIPolicyParams = Cast<UDisplayClusterProjectionPolicyMPCDIParameters>(PolicyParameters);
	if (MPCDIPolicyParams && MPCDIPolicyParams->Origin)
	{
		Views.AddDefaulted(1);
		SetOriginComp(MPCDIPolicyParams->Origin);
		WarpRef = MPCDIPolicyParams->MPCDILocator;
	}
}

UMeshComponent* FDisplayClusterProjectionMPCDIPolicy::BuildMeshPreview(UDisplayClusterProjectionPolicyParameters* PolicyParameters)
{
	UDisplayClusterProjectionPolicyMPCDIParameters* MPCDIPolicyParams = Cast<UDisplayClusterProjectionPolicyMPCDIParameters>(PolicyParameters);
	if (MPCDIPolicyParams)
	{
		FScopeLock Lock(&MPCDIPolicyParams->InternalsSyncScope);
		if (MPCDIPolicyParams->MPCDILocator.IsValid() && MPCDIPolicyParams->Origin)
		{
			IMPCDI& MPCDI = IMPCDI::Get();

			FMPCDIGeometryExportData MeshData;
			if (!MPCDI.GetMPCDIMeshData(MPCDIPolicyParams->File, MPCDIPolicyParams->Buffer, MPCDIPolicyParams->Region, MeshData))
			{
				return nullptr;
			}

			const FString CompName = FString::Printf(TEXT("MPCDI_%s_%s_impl"), *MPCDIPolicyParams->Buffer, *MPCDIPolicyParams->Region);
			UProceduralMeshComponent* MeshComp = NewObject<UProceduralMeshComponent>(MPCDIPolicyParams->Origin, FName(*CompName), EObjectFlags::RF_DuplicateTransient | RF_Transient | RF_TextExportTransient);

			MeshComp->RegisterComponent();
			MeshComp->AttachToComponent(MPCDIPolicyParams->Origin, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));
			MeshComp->CreateMeshSection(0, MeshData.Vertices, MeshData.Triangles, MeshData.Normal, MeshData.UV, TArray<FColor>(), TArray<FProcMeshTangent>(), false);

			return MeshComp;
		}
	}

	return nullptr;
}

void FDisplayClusterProjectionMPCDIPolicy::RenderFrame(USceneComponent* Camera, UDisplayClusterProjectionPolicyParameters* PolicyParameters, FTextureRenderTargetResource* RenderTarget, FIntRect RenderRegion, bool bApplyWarpBlend)
{
	if (RenderTarget && PolicyParameters && Camera)
	{
		UDisplayClusterProjectionPolicyMPCDIParameters* MPCDIPolicyParams = Cast<UDisplayClusterProjectionPolicyMPCDIParameters>(PolicyParameters);
		if (MPCDIPolicyParams)
		{
			FScopeLock Lock(&MPCDIPolicyParams->InternalsSyncScope);
			if (MPCDIPolicyParams->MPCDILocator.IsValid())
			{
				FDisplayClusterRenderingParameters PreviewParameters;

				PreviewParameters.RenderTarget = RenderTarget;
				PreviewParameters.RenderTargetRect = RenderRegion;
				PreviewParameters.ProjectionPolicy = this;
				PreviewParameters.bAllowWarpBlend = bApplyWarpBlend;
				PreviewParameters.HiddenActors.Add(MPCDIPolicyParams->RootActor);

				// World scale multiplier
				UWorld* World = Camera->GetWorld();
				PreviewParameters.Scene = World->Scene;
				const float WorldScale = World->GetWorldSettings()->WorldToMeters / 100.f;

				PreviewParameters.ViewLocation = Camera->GetComponentLocation();
				PreviewParameters.ViewRotation = Camera->GetComponentRotation();
				PreviewParameters.ProjectionMatrix = FMatrix::Identity;

				ViewportSize = RenderTarget->GetSizeXY();

				if (CalculateView(0, PreviewParameters.ViewLocation, PreviewParameters.ViewRotation, FVector::ZeroVector, WorldScale, 1.f, 1.f))
				{
					if (GetProjectionMatrix(0, PreviewParameters.ProjectionMatrix))
					{
						IDisplayClusterRendering::Get().RenderSceneToTexture(PreviewParameters);
					}
				}
			}
		}
	}
}
#endif
