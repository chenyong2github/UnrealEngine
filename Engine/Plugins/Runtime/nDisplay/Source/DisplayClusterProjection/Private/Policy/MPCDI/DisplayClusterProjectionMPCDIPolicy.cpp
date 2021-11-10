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
{
}

FDisplayClusterProjectionMPCDIPolicy::~FDisplayClusterProjectionMPCDIPolicy()
{
	ImplRelease();
}

void FDisplayClusterProjectionMPCDIPolicy::UpdateProxyData(IDisplayClusterViewport* InViewport)
{
	check(InViewport);

	const TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe> ProjectionPolicyPtr = InViewport->GetProjectionPolicy();
	const TSharedPtr<IDisplayClusterWarpBlend, ESPMode::ThreadSafe> WarpBlendInterfacePtr = WarpBlendInterface;

	ENQUEUE_RENDER_COMMAND(DisplayClusterProjectionMPCDIPolicy_UpdateProxyData)(
		[ProjectionPolicyPtr, WarpBlendInterfacePtr, Contexts = WarpBlendContexts](FRHICommandListImmediate& RHICmdList)
	{
		IDisplayClusterProjectionPolicy* ProjectionPolicy = ProjectionPolicyPtr.Get();
		if (ProjectionPolicy)
		{
			FDisplayClusterProjectionMPCDIPolicy* MPCDIPolicy = static_cast<FDisplayClusterProjectionMPCDIPolicy*>(ProjectionPolicy);
			if (MPCDIPolicy)
			{
				MPCDIPolicy->WarpBlendInterface_Proxy = WarpBlendInterfacePtr;
				MPCDIPolicy->WarpBlendContexts_Proxy = Contexts;
			}
		}
	});
}

//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicy
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterProjectionMPCDIPolicy::HandleStartScene(IDisplayClusterViewport* InViewport)
{
	if (bInvalidConfiguration)
	{
		return false;
	}

	check(IsInGameThread());

	// The game side of the nDisplay has been initialized by the nDisplay Game Manager already
	// so we can extend it by our projection related functionality/components/etc.

	WarpBlendContexts.Empty();

	if (WarpBlendInterface.IsValid() == false && !CreateWarpBlendFromConfig())
	{
		// Ignore broken MPCDI config for other attempts
		bInvalidConfiguration = true;

		if (!IsEditorOperationMode())
		{
			UE_LOG(LogDisplayClusterProjectionMPCDI, Warning, TEXT("Couldn't load MPCDI config for viewport '%s'"), *InViewport->GetId());
		}

		return false;
	}

	// Find origin component if it exists
	InitializeOriginComponent(InViewport, OriginCompId);

	// Finally, initialize internal views data container
	WarpBlendContexts.AddDefaulted(2);

	return true;
}

void FDisplayClusterProjectionMPCDIPolicy::HandleEndScene(IDisplayClusterViewport* InViewport)
{
	check(IsInGameThread());

	ImplRelease();

#if WITH_EDITOR
	ReleasePreviewMeshComponent();
#endif
}

void FDisplayClusterProjectionMPCDIPolicy::ImplRelease()
{
	ReleaseOriginComponent();

	WarpBlendInterface.Reset();
	WarpBlendContexts.Empty();
}

bool FDisplayClusterProjectionMPCDIPolicy::CalculateView(IDisplayClusterViewport* InViewport, const uint32 InContextNum, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP)
{
	check(IsInGameThread());

	if (WarpBlendInterface.IsValid() == false || WarpBlendContexts.Num() == 0)
	{
		if (!IsEditorOperationMode())
		{
			UE_LOG(LogDisplayClusterProjectionMPCDI, Warning, TEXT("Invalid warp data for viewport '%s'"), *InViewport->GetId());
		}

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

	if (InContextNum < (uint32)WarpBlendContexts.Num())
	{
		OutPrjMatrix = WarpBlendContexts[InContextNum].ProjectionMatrix;

		return true;
	}
	
	return false;
}

bool FDisplayClusterProjectionMPCDIPolicy::IsWarpBlendSupported()
{
	return true;
}

void FDisplayClusterProjectionMPCDIPolicy::ApplyWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, const IDisplayClusterViewportProxy* InViewportProxy)
{
	check(IsInRenderingThread());

	if (WarpBlendInterface_Proxy.IsValid() == false || WarpBlendContexts_Proxy.Num() == 0)
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

	IDisplayClusterShaders& ShadersAPI = IDisplayClusterShaders::Get();

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

							// Support stereo icvfx
							for (FDisplayClusterShaderParameters_ICVFX::FCameraSettings& CameraIt : ShaderICVFX.Cameras)
							{
								if (It->ViewportId == CameraIt.Resource.ViewportId)
								{
									// matched camera reference, update context for current eye
									const FDisplayClusterViewport_Context& InContext = RefViewportProxy->GetContexts_RenderThread()[ContextNum];

									FDisplayClusterShaderParametersICVFX_CameraContext CameraContext;
									CameraContext.CameraViewLocation = InContext.ViewLocation;
									CameraContext.CameraViewRotation = InContext.ViewRotation;
									CameraContext.CameraPrjMatrix = InContext.ProjectionMatrix;

									CameraIt.UpdateCameraContext(CameraContext);
								}
							}
						}
					}
				}

				// Initialize shader input data
				FDisplayClusterShaderParameters_WarpBlend WarpBlendParameters;

				WarpBlendParameters.Context = WarpBlendContexts_Proxy[ContextNum];
				WarpBlendParameters.WarpInterface = WarpBlendInterface_Proxy;

				WarpBlendParameters.Src.Set(InputTextures[ContextNum], InputRects[ContextNum]);
				WarpBlendParameters.Dest.Set(OutputTextures[ContextNum], OutputRects[ContextNum]);

				if (!ShadersAPI.RenderWarpBlend_ICVFX(RHICmdList, WarpBlendParameters, ShaderICVFX))
				{
					if (!IsEditorOperationMode())
					{
						UE_LOG(LogDisplayClusterProjectionMPCDI, Warning, TEXT("Couldn't apply icvfx warp&blend"));
					}

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

		WarpBlendParameters.Context = WarpBlendContexts_Proxy[ContextNum];
		WarpBlendParameters.WarpInterface = WarpBlendInterface_Proxy;

		WarpBlendParameters.Src.Set(InputTextures[ContextNum], InputRects[ContextNum]);
		WarpBlendParameters.Dest.Set(OutputTextures[ContextNum], OutputRects[ContextNum]);


		if (!ShadersAPI.RenderWarpBlend_MPCDI(RHICmdList, WarpBlendParameters))
		{
			if (!IsEditorOperationMode())
			{
				UE_LOG(LogDisplayClusterProjectionMPCDI, Warning, TEXT("Couldn't apply mpcdi warp&blend"));
			}

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

UMeshComponent* FDisplayClusterProjectionMPCDIPolicy::GetOrCreatePreviewMeshComponent(class IDisplayClusterViewport* InViewport, bool& bOutIsRootActorComponent)
{
	check(IsInGameThread());

	if (WarpBlendInterface.IsValid() == false || !bIsPreviewMeshEnabled)
	{
		return nullptr;
	}

	// used created mesh component
	bOutIsRootActorComponent = false;

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

	// Downscale preview mesh dimension to max limit
	const uint32 PreviewGeometryDimLimit = 128;

	// Create new WarpMesh component
	FMPCDIGeometryExportData MeshData;
	if (WarpBlendInterface->ExportWarpMapGeometry(&MeshData, PreviewGeometryDimLimit))
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
		IDisplayClusterShaders& ShadersAPI = IDisplayClusterShaders::Get();

		// Support custom origin node
		OriginCompId = CfgData.OriginType;

		bIsPreviewMeshEnabled = CfgData.bEnablePreview;

		// Load from MPCDI file:
		if (CfgData.PFMFile.IsEmpty())
		{
			// Check if MPCDI file exists
			if (CfgData.MPCDIFileName.IsEmpty())
			{
				if (!IsEditorOperationMode())
				{
					UE_LOG(LogDisplayClusterProjectionMPCDI, Warning, TEXT("File not found: Empty"));
				}

				return false;
			}

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

			UE_LOG(LogDisplayClusterProjectionMPCDI, Verbose, TEXT("MPCDI policy has been initialized [%s:%s in %s]"), *CfgData.BufferId, *CfgData.RegionId, *CfgData.MPCDIFileName);
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

		UE_LOG(LogDisplayClusterProjectionMPCDI, Verbose, TEXT("MPCDI policy has been initialized from PFM '%s"), *CfgData.PFMFile);
		return true;
	}

	return false;
}

