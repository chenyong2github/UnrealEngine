// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Policy/MPCDI/DisplayClusterProjectionMPCDIPolicy.h"

#include "DisplayClusterProjectionHelpers.h"
#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"

#include "Config/DisplayClusterConfigTypes.h"
#include "Game/IDisplayClusterGameManager.h"

#include "Engine/RendererSettings.h"
#include "Misc/Paths.h"


FDisplayClusterProjectionMPCDIPolicy::FDisplayClusterProjectionMPCDIPolicy(const FString& ViewportId)
	: FDisplayClusterProjectionPolicyBase(ViewportId)
	, MPCDIAPI(IMPCDI::Get())
	, bIsRenderResourcesInitialized(false)
{
}

FDisplayClusterProjectionMPCDIPolicy::~FDisplayClusterProjectionMPCDIPolicy()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicy
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterProjectionMPCDIPolicy::StartScene(UWorld* World)
{
	check(IsInGameThread());

	// The game side of the nDisplay has been initialized by the nDisplay Game Manager already
	// so we can extend it by our projection related functionality/components/etc.

	// Find origin component if it exists
	InitializeOriginComponent(OriginCompId);


}

void FDisplayClusterProjectionMPCDIPolicy::EndScene()
{
	check(IsInGameThread());
}

bool FDisplayClusterProjectionMPCDIPolicy::HandleAddViewport(const FIntPoint& InViewportSize, const uint32 InViewsAmount)
{
	check(IsInGameThread());
	check(InViewsAmount > 0);

	IMPCDI::ConfigParser CfgData;
	{//Load config:
		FDisplayClusterConfigProjection CfgProjection;
		if (!DisplayClusterHelpers::config::GetViewportProjection(GetViewportId(), CfgProjection))
		{
			UE_LOG(LogDisplayClusterProjectionMPCDI, Error, TEXT("Couldn't obtain projection info for '%s' viewport"), *GetViewportId());
			return false;
		}

		if (!MPCDIAPI.LoadConfig(CfgProjection.Params, CfgData))
		{
			UE_LOG(LogDisplayClusterProjectionMPCDI, Error, TEXT("Couldn't read MPCDI configuration from the config file"));
			return false;
		}
	}

	// Load MPCDI config
	if (!MPCDIAPI.Load(CfgData, WarpRef))
	{
		UE_LOG(LogDisplayClusterProjectionMPCDI, Warning, TEXT("Couldn't load MPCDI config: %s"), *CfgData.ConfigLineStr);
		return false;
	}

	UE_LOG(LogDisplayClusterProjectionMPCDI, Log, TEXT("MPCDI policy has been initialized [%s:%s in %s]"), *CfgData.BufferId, *CfgData.RegionId, *CfgData.MPCDIFileName);

	// Finally, initialize internal views data container
	Views.AddDefaulted(InViewsAmount);
	ViewportSize = InViewportSize;

	return true;
}

void FDisplayClusterProjectionMPCDIPolicy::HandleRemoveViewport()
{
	check(IsInGameThread());
}

bool FDisplayClusterProjectionMPCDIPolicy::CalculateView(const uint32 ViewIdx, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP)
{
	check(IsInGameThread());

	// World scale multiplier
	const float WorldScale = WorldToMeters / 100.f;

	// Get view location in local space
	const USceneComponent* const OriginComp = GetOriginComp();
	const FTransform& World2LocalTransform = (OriginComp ? OriginComp->GetComponentTransform() : FTransform::Identity);

	// Get our base camera location and view offset in local space (MPCDI space)
	const FVector LocalOrigin    = World2LocalTransform.InverseTransformPosition(InOutViewLocation - ViewOffset);
	const FVector LocalEyeOrigin = World2LocalTransform.InverseTransformPosition(InOutViewLocation);

	// Initialize frustum
	IMPCDI::FFrustum& OutFrustum = Views[ViewIdx].Frustum;
	OutFrustum.OriginLocation  = LocalOrigin;
	OutFrustum.OriginEyeOffset = LocalEyeOrigin - LocalOrigin;

	if (Views[ViewIdx].RTTexture != nullptr)
	{
		OutFrustum.ViewportSize = Views[ViewIdx].RTTexture->GetSizeXY();
	}

	// Compute frustum
	if (!MPCDIAPI.ComputeFrustum(WarpRef, WorldScale, NCP, FCP, OutFrustum))
	{
		return false;
	}

	// Get rotation in warp space
	const FRotator MpcdiRotation = Views[ViewIdx].Frustum.Local2WorldMatrix.Rotator();
	const FVector  MpcdiOrigin = Views[ViewIdx].Frustum.Local2WorldMatrix.GetOrigin();

	// Transform rotation to world space
	InOutViewRotation = World2LocalTransform.TransformRotation(MpcdiRotation.Quaternion()).Rotator();
	InOutViewLocation = World2LocalTransform.TransformPosition(MpcdiOrigin);

	OutFrustum.bIsValid = true;

	return true;
}

bool FDisplayClusterProjectionMPCDIPolicy::GetProjectionMatrix(const uint32 ViewIdx, FMatrix& OutPrjMatrix)
{
	check(IsInGameThread());

	OutPrjMatrix = Views[ViewIdx].Frustum.ProjectionMatrix;
	
	return true;
}

bool FDisplayClusterProjectionMPCDIPolicy::IsWarpBlendSupported()
{
	return true;
}

void FDisplayClusterProjectionMPCDIPolicy::ApplyWarpBlend_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect)
{
	check(IsInRenderingThread());

	if (!InitializeResources_RenderThread())
	{
		UE_LOG(LogDisplayClusterProjectionMPCDI, Warning, TEXT("Couldn't initialize rendering resources"));
		return;
	}

	// Initialize shader input data
	IMPCDI::FShaderInputData ShaderInputData;
	ShaderInputData.RegionLocator = WarpRef;
	ShaderInputData.Frustum = Views[ViewIdx].Frustum;

	// Initialize texture data
	IMPCDI::FTextureWarpData TextureWarpData;
	TextureWarpData.SrcTexture = SrcTexture;
	TextureWarpData.SrcRect = ViewportRect;

	TextureWarpData.DstTexture = Views[ViewIdx].RTTexture;
	TextureWarpData.DstRect = FIntRect(FIntPoint(0, 0), ViewportSize);

	// Perform warp&blend
	if (!MPCDIAPI.ApplyWarpBlend(RHICmdList, TextureWarpData, ShaderInputData))
	{
		UE_LOG(LogDisplayClusterProjectionMPCDI, Warning, TEXT("Couldn't apply warp&blend"));
		return;
	}

	// Copy result back to the render target
	FResolveParams copyParams;
	copyParams.DestArrayIndex = 0;
	copyParams.SourceArrayIndex = 0;

	copyParams.Rect.X1 = TextureWarpData.DstRect.Min.X;
	copyParams.Rect.X2 = TextureWarpData.DstRect.Max.X;
	copyParams.Rect.Y1 = TextureWarpData.DstRect.Min.Y;
	copyParams.Rect.Y2 = TextureWarpData.DstRect.Max.Y;

	copyParams.DestRect.X1 = ViewportRect.Min.X;
	copyParams.DestRect.Y1 = ViewportRect.Min.Y;
	copyParams.DestRect.X2 = ViewportRect.Max.X;
	copyParams.DestRect.Y2 = ViewportRect.Max.Y;

	RHICmdList.CopyToResolveTarget(Views[ViewIdx].RTTexture, SrcTexture, copyParams);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterProjectionMPCDIPolicy
//////////////////////////////////////////////////////////////////////////////////////////////

bool FDisplayClusterProjectionMPCDIPolicy::InitializeResources_RenderThread()
{
	check(IsInRenderingThread());

	if (!bIsRenderResourcesInitialized)
	{
		FScopeLock lock(&RenderingResourcesInitializationCS);
		if (!bIsRenderResourcesInitialized)
		{
			static const TConsoleVariableData<int32>* CVarDefaultBackBufferPixelFormat = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DefaultBackBufferPixelFormat"));
			static const EPixelFormat SceneTargetFormat = EDefaultBackBufferPixelFormat::Convert2PixelFormat(EDefaultBackBufferPixelFormat::FromInt(CVarDefaultBackBufferPixelFormat->GetValueOnRenderThread()));

			// Create RT texture for viewport warp
			for (auto& It : Views)
			{
				FRHIResourceCreateInfo CreateInfo;
				FTexture2DRHIRef DummyTexRef;
				RHICreateTargetableShaderResource2D(ViewportSize.X, ViewportSize.Y, SceneTargetFormat, 1, TexCreate_None, TexCreate_RenderTargetable, false, CreateInfo, It.RTTexture, DummyTexRef);
			}

			bIsRenderResourcesInitialized = true;
		}
	}

	return bIsRenderResourcesInitialized;
}
