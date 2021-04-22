// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/MPCDI/DisplayClusterProjectionMPCDIPolicy.h"
#include "Policy/MPCDI/DisplayClusterProjectionMPCDIPolicy_Config.h"

#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"

#include "IDisplayCluster.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "Game/IDisplayClusterGameManager.h"

#include "Misc/DisplayClusterHelpers.h"

#include "Engine/RendererSettings.h"
#include "Misc/Paths.h"

#include "Render/Viewport/IDisplayClusterViewportManager.h"
#include "Render/Viewport/IDisplayClusterViewportManagerProxy.h"
#include "Render/Viewport/IDisplayClusterViewport.h"
#include "Render/Viewport/IDisplayClusterViewportProxy.h"

#include "IDisplayClusterShaders.h"
#include "WarpBlend/IDisplayClusterWarpBlend.h"
#include "WarpBlend/IDisplayClusterWarpBlendManager.h"

#include "Render/Viewport/Containers/DisplayClusterViewport_RenderSettingsICVFX.h"

#include "ShaderParameters/DisplayClusterShaderParameters_WarpBlend.h"

#include "Blueprints/MPCDIGeometryData.h"
#include "DisplayClusterRootActor.h"

FDisplayClusterProjectionMPCDIPolicy::FDisplayClusterProjectionMPCDIPolicy(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy)
	: FDisplayClusterProjectionPolicyBase(ProjectionPolicyId, InConfigurationProjectionPolicy)
	, ShadersAPI(IDisplayClusterShaders::Get())
{
}

FDisplayClusterProjectionMPCDIPolicy::~FDisplayClusterProjectionMPCDIPolicy()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicy
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterProjectionMPCDIPolicy::HandleStartScene(IDisplayClusterViewport* InViewport)
{
	check(IsInGameThread());

	// The game side of the nDisplay has been initialized by the nDisplay Game Manager already
	// so we can extend it by our projection related functionality/components/etc.

	// Find origin component if it exists
	InitializeOriginComponent(InViewport, OriginCompId);

	if (WarpBlendInterface == nullptr)
	{
		if (!CreateWarpBlendFromConfig())
		{
			UE_LOG(LogDisplayClusterProjectionMPCDI, Warning, TEXT("Couldn't load MPCDI config"));
			return false;
		}
	}

	// Finally, initialize internal views data container
	WarpBlendContexts.Empty();
	WarpBlendContexts.AddDefaulted(2);

	return true;
}

void FDisplayClusterProjectionMPCDIPolicy::HandleEndScene(IDisplayClusterViewport* InViewport)
{
	check(IsInGameThread());

	ReleaseOriginComponent(InViewport);
}

bool FDisplayClusterProjectionMPCDIPolicy::CalculateView(IDisplayClusterViewport* InViewport, const uint32 InContextNum, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP)
{
	check(IsInGameThread());

	if (WarpBlendInterface == nullptr)
	{
		UE_LOG(LogDisplayClusterProjectionMPCDI, Warning, TEXT("Invalid warp data for viewport '%s'"), *InViewport->GetId());
		return false;
	}

	// World scale multiplier
	const float WorldScale = WorldToMeters / 100.f;

	// Get view location in local space
	const USceneComponent* const OriginComp = GetOriginComp();
	const FTransform& World2LocalTransform = (OriginComp ? OriginComp->GetComponentTransform() : FTransform::Identity);

	// Get our base camera location and view offset in local space (MPCDI space)
	const FVector LocalOrigin    = World2LocalTransform.InverseTransformPosition(InOutViewLocation - ViewOffset);
	const FVector LocalEyeOrigin = World2LocalTransform.InverseTransformPosition(InOutViewLocation);

	// Initialize frustum
	FDisplayClusterWarpEye Eye;
	Eye.OriginLocation  = LocalOrigin;
	Eye.OriginEyeOffset = LocalEyeOrigin - LocalOrigin;
	Eye.WorldScale = WorldScale;
	Eye.ZNear = NCP;
	Eye.ZFar  = FCP;

	// Compute frustum
	if (!WarpBlendInterface->CalcFrustumContext(InViewport, InContextNum, Eye, WarpBlendContexts[InContextNum]))
	{
		return false;
	}

	// Get rotation in warp space
	const FRotator MpcdiRotation = WarpBlendContexts[InContextNum].OutCameraRotation;
	const FVector  MpcdiOrigin  = WarpBlendContexts[InContextNum].OutCameraOrigin;

	// Transform rotation to world space
	InOutViewRotation = World2LocalTransform.TransformRotation(MpcdiRotation.Quaternion()).Rotator();
	InOutViewLocation = World2LocalTransform.TransformPosition(MpcdiOrigin);

	WarpBlendContexts[InContextNum].bIsValid = true;

	return true;
}

bool FDisplayClusterProjectionMPCDIPolicy::GetProjectionMatrix(IDisplayClusterViewport* InViewport, const uint32 InContextNum, FMatrix& OutPrjMatrix)
{
	check(IsInGameThread());

	OutPrjMatrix = WarpBlendContexts[InContextNum].ProjectionMatrix;
	
	return true;
}

bool FDisplayClusterProjectionMPCDIPolicy::IsWarpBlendSupported()
{
	return true;
}

void FDisplayClusterProjectionMPCDIPolicy::ApplyWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const IDisplayClusterViewportProxy* InViewportProxy)
{
	check(IsInRenderingThread());

	if (WarpBlendInterface == nullptr)
	{
		return;
	}

	TArray<FRHITexture2D*> InputTextures, OutputTextures;
	TArray<FIntRect> InputRects, OutputRects;

	// Use for input first MipsShader texture if enabled in viewport render settings
	if (!InViewportProxy->GetResourcesWithRects_RenderThread(EDisplayClusterViewportResourceType::MipsShaderResource, InputTextures, InputRects))
	{
		// otherwise inputshader texture
		if (!InViewportProxy->GetResourcesWithRects_RenderThread(EDisplayClusterViewportResourceType::InputShaderResource, InputTextures, InputRects))
		{
			// no source textures
			return;
		}
	}

	// Get output resources with rects
	if (!InViewportProxy->GetResourcesWithRects_RenderThread(InViewportProxy->GetOutputResourceType(), OutputTextures, OutputRects))
	{
		return;
	}


	const FDisplayClusterViewport_RenderSettingsICVFX& SettingsICVFX = InViewportProxy->GetRenderSettingsICVFX_RenderThread();
	if ((SettingsICVFX.RuntimeFlags & ViewportRuntime_ICVFXTarget) != 0)
	{
		// This viewport used as icvfx target:
		TArray<FDisplayClusterShaderParametersICVFX_ViewportResource*> ViewportResources;
		FDisplayClusterShaderParameters_ICVFX ShaderICVFX(SettingsICVFX.ICVFX);

		// Collect refs
		ShaderICVFX.CollectRefViewports(ViewportResources);
		{
			// ICVFX warp:
			for (int ContextNum = 0; ContextNum < InputTextures.Num(); ContextNum++)
			{
				// Update ref viewport resources:
				for (FDisplayClusterShaderParametersICVFX_ViewportResource* It : ViewportResources)
				{
					// reset prev resource ref
					It->Texture = nullptr;

					const IDisplayClusterViewportProxy* RefViewportProxy = InViewportProxy->GetOwner().FindViewport_RenderThread(It->ViewportId);
					if (RefViewportProxy)
					{
						TArray<FRHITexture2D*> RefTextures;
						// Use for input first MipsShader texture if enabled in viewport render settings
						if (RefViewportProxy->GetResources_RenderThread(EDisplayClusterViewportResourceType::MipsShaderResource, RefTextures) ||
							RefViewportProxy->GetResources_RenderThread(EDisplayClusterViewportResourceType::InputShaderResource, RefTextures))
						{
							int ReContextNum = FMath::Min(ContextNum, RefTextures.Num());
							It->Texture = RefTextures[ReContextNum];
						}
					}
				}

				// Initialize shader input data
				FDisplayClusterShaderParameters_WarpBlend WarpBlendParameters;

				WarpBlendParameters.Context = WarpBlendContexts[ContextNum];
				WarpBlendParameters.WarpInterface = WarpBlendInterface;

				WarpBlendParameters.Src.Set(InputTextures[ContextNum], InputRects[ContextNum]);
				WarpBlendParameters.Dest.Set(OutputTextures[ContextNum], OutputRects[ContextNum]);

				if (!ShadersAPI.RenderWarpBlend_ICVFX(RHICmdList, WarpBlendParameters, ShaderICVFX))
				{
					UE_LOG(LogDisplayClusterProjectionMPCDI, Warning, TEXT("Couldn't apply icvfx warp&blend"));
					return;
				}

			}

			// Finish ICVFX warp
			return;
		}
	}
	
	// Mesh warp:
	for (int ContextNum = 0; ContextNum < InputTextures.Num(); ContextNum++)
	{
		// Initialize shader input data
		FDisplayClusterShaderParameters_WarpBlend WarpBlendParameters;

		WarpBlendParameters.Context = WarpBlendContexts[ContextNum];
		WarpBlendParameters.WarpInterface = WarpBlendInterface;

		WarpBlendParameters.Src.Set(InputTextures[ContextNum], InputRects[ContextNum]);
		WarpBlendParameters.Dest.Set(OutputTextures[ContextNum], OutputRects[ContextNum]);


		if (!ShadersAPI.RenderWarpBlend_MPCDI(RHICmdList, WarpBlendParameters))
		{
			UE_LOG(LogDisplayClusterProjectionMPCDI, Warning, TEXT("Couldn't apply mpcdi warp&blend"));
			return;
		}

	}
}



#if WITH_EDITOR

#include "ProceduralMeshComponent.h"

void FDisplayClusterProjectionMPCDIPolicy::ReleasePreviewMeshComponent()
{
	USceneComponent* PreviewMeshComp = PreviewMeshComponentRef.GetOrFindSceneComponent();
	if (PreviewMeshComp != nullptr)
	{
		PreviewMeshComp->UnregisterComponent();
		PreviewMeshComp->DestroyComponent();
	}

	PreviewMeshComponentRef.ResetSceneComponent();
}

UMeshComponent* FDisplayClusterProjectionMPCDIPolicy::GetOrCreatePreviewMeshComponent(class IDisplayClusterViewport* InViewport)
{
	check(IsInGameThread());

	if (WarpBlendInterface == nullptr)
	{
		return nullptr;
	}

	USceneComponent* OriginComp = GetOriginComp();

	// Return Exist mesh component
	USceneComponent* PreviewMeshComp = PreviewMeshComponentRef.GetOrFindSceneComponent();
	if (PreviewMeshComp != nullptr)
	{
		UProceduralMeshComponent* PreviewMesh = Cast<UProceduralMeshComponent>(PreviewMeshComp);
		if (PreviewMesh != nullptr)
		{
			// update attachment to parent
			PreviewMesh->AttachToComponent(OriginComp, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));
			return PreviewMesh;
		}
	}

	// Create new WarpMesh component
	FMPCDIGeometryExportData MeshData;
	if (WarpBlendInterface->ExportWarpMapGeometry(&MeshData))
	{
		const FString CompName = FString::Printf(TEXT("MPCDI_%s_impl"), *GetId());

		// Creta new object
		UProceduralMeshComponent* MeshComp = NewObject<UProceduralMeshComponent>(OriginComp, FName(*CompName), EObjectFlags::RF_DuplicateTransient | RF_Transient | RF_TextExportTransient);
		if (MeshComp)
		{
			MeshComp->RegisterComponent();
			MeshComp->AttachToComponent(OriginComp, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));
			MeshComp->CreateMeshSection(0, MeshData.Vertices, MeshData.Triangles, MeshData.Normal, MeshData.UV, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
			MeshComp->SetIsVisualizationComponent(true);

			// Store reference to mesh component
			PreviewMeshComponentRef.SetSceneComponent(MeshComp);
			return MeshComp;
		}
	}

	return nullptr;
}

#endif

bool FDisplayClusterProjectionMPCDIPolicy::CreateWarpBlendFromConfig()
{
	check(IsInGameThread());

	bool bResult = false;

	FConfigParser CfgData;
	if (CfgData.ImplLoadConfig(GetParameters()))
	{
		// Support custom origin node
		OriginCompId = CfgData.OriginType;

		// Load from MPCDI file:
		if (CfgData.PFMFile.IsEmpty())
		{
			// Check if MPCDI file exists
			if (!FPaths::FileExists(CfgData.MPCDIFileName))
			{
				UE_LOG(LogDisplayClusterProjectionMPCDI, Warning, TEXT("File not found: %s"), *CfgData.MPCDIFileName);
				return false;
			}

			FDisplayClusterWarpBlendConstruct::FLoadMPCDIFile CreateParameters;
			CreateParameters.MPCDIFileName = CfgData.MPCDIFileName;
			CreateParameters.BufferId = CfgData.BufferId;
			CreateParameters.RegionId = CfgData.RegionId;

			if (!ShadersAPI.GetWarpBlendManager().Create(CreateParameters, WarpBlendInterface))
			{
				UE_LOG(LogDisplayClusterProjectionMPCDI, Warning, TEXT("Couldn't load MPCDI file: %s"), *CfgData.MPCDIFileName);
				return false;
			}

			UE_LOG(LogDisplayClusterProjectionMPCDI, Log, TEXT("MPCDI policy has been initialized [%s:%s in %s]"), *CfgData.BufferId, *CfgData.RegionId, *CfgData.MPCDIFileName);
			return true;
		}

		FDisplayClusterWarpBlendConstruct::FLoadPFMFile CreateParameters;
		CreateParameters.ProfileType = CfgData.MPCDIType;
		CreateParameters.PFMFileName = CfgData.PFMFile;

		CreateParameters.PFMScale = CfgData.PFMFileScale;
		CreateParameters.bIsUnrealGameSpace = CfgData.bIsUnrealGameSpace;

		CreateParameters.AlphaMapFileName = CfgData.AlphaFile;
		CreateParameters.AlphaMapEmbeddedAlpha = CfgData.AlphaGamma;

		CreateParameters.BetaMapFileName = CfgData.BetaFile;

		if (!ShadersAPI.GetWarpBlendManager().Create(CreateParameters, WarpBlendInterface))
		{
			UE_LOG(LogDisplayClusterProjectionMPCDI, Error, TEXT("Failed to load PFM from file: %s"), *CfgData.PFMFile);
			return false;
		}

		UE_LOG(LogDisplayClusterProjectionMPCDI, Log, TEXT("MPCDI policy has been initialized from PFM '%s"), *CfgData.PFMFile);
		return true;
	}

	return false;
}

