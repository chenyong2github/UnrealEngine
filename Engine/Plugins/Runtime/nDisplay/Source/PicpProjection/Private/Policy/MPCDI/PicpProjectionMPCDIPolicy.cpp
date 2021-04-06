// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/MPCDI/PicpProjectionMPCDIPolicy.h"

#include "PicpProjectionLog.h"
#include "PicpProjectionStrings.h"

#include "IDisplayCluster.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "Game/IDisplayClusterGameManager.h"
#include "Render/IDisplayClusterRenderManager.h"
#include "Render/Device/IDisplayClusterRenderDevice.h"

#include "Misc/DisplayClusterHelpers.h"
#include "Misc/Paths.h"

#include "Engine/RendererSettings.h"
#include "Engine/TextureRenderTarget2D.h"

#include "RenderTargetPool.h"

static TAutoConsoleVariable<int32> CVarPicpShowFakeCamera(
	TEXT("nDisplay.render.picp.ShowFakeCamera"),
	0,
	TEXT("Show fake debug camera\n")
	TEXT(" 0: disabled\n")
	TEXT(" 1: enabled\n"),
	ECVF_RenderThreadSafe
);

FPicpProjectionMPCDIPolicy::FPicpProjectionMPCDIPolicy(FPicpProjectionModule& InPicpProjectionModule, const FString& ViewportId, const TMap<FString, FString>& Parameters)
	: FPicpProjectionPolicyBase(InPicpProjectionModule, ViewportId, Parameters)
	, PicpMPCDIAPI(IPicpMPCDI::Get())
	, MPCDIAPI(IMPCDI::Get())
{
}

FPicpProjectionMPCDIPolicy::~FPicpProjectionMPCDIPolicy()
{
}

//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicy
//////////////////////////////////////////////////////////////////////////////////////////////
void FPicpProjectionMPCDIPolicy::StartScene(UWorld* World)
{
	check(IsInGameThread());

	// The game side of the nDisplay has been initialized by the nDisplay Game Manager already
	// so we can extend it by our projection related functionality/components/etc.
	// Find origin component if it exists
	InitializeOriginComponent(OriginCompId);
}

void FPicpProjectionMPCDIPolicy::EndScene()
{
	check(IsInGameThread());

	ReleaseOriginComponent();
}

bool FPicpProjectionMPCDIPolicy::HandleAddViewport(const FIntPoint& InViewportSize, const uint32 InViewsAmount)
{
	check(IsInGameThread());
	check(InViewsAmount > 0);

	IMPCDI::ConfigParser CfgData;

	if (!MPCDIAPI.LoadConfig(GetParameters(), CfgData))
	{
		UE_LOG(LogPicpProjectionMPCDI, Error, TEXT("Couldn't read MPCDI configuration from the config file"));
		return false;
	}

	// Load MPCDI config
	FScopeLock lock(&WarpRefCS);
	if (!MPCDIAPI.Load(CfgData, WarpRef))
	{
		UE_LOG(LogPicpProjectionMPCDI, Warning, TEXT("Couldn't initialize PICP MPCDI for viewport %s"), *GetViewportId());
		return false;
	}

	// Support custom origin node
	OriginCompId = CfgData.OriginType;

	UE_LOG(LogPicpProjectionMPCDI, Log, TEXT("PICP MPCDI policy for viewport %s has been initialized"), *GetViewportId());

	// Finally, initialize internal views data container
	Views.AddDefaulted(InViewsAmount);
	SetViewportSize(InViewportSize);

	return true;
}

void FPicpProjectionMPCDIPolicy::HandleRemoveViewport()
{
	check(IsInGameThread());
}

bool FPicpProjectionMPCDIPolicy::CalculateView(const uint32 ViewIdx, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP)
{
	check(IsInGameThread());

	FScopeLock lock(&WarpRefCS);
	if (!WarpRef.IsValid())
	{
		UE_LOG(LogPicpProjectionMPCDI, Warning, TEXT("Picp Warp data not assigned for viewport '%s'"), *GetViewportId());
		return false;
	}

	// World scale multiplier
	const float WorldScale = WorldToMeters / 100.f;

	// Get view location in local space
	const USceneComponent* const OriginComp = GetOriginComp();
	const FTransform& World2LocalTransform = (OriginComp ? OriginComp->GetComponentTransform() : FTransform::Identity);

	// Get our base camera location and view offset in local space (MPCDI space)
	const FVector LocalOrigin = World2LocalTransform.InverseTransformPosition(InOutViewLocation - ViewOffset);
	const FVector LocalEyeOrigin = World2LocalTransform.InverseTransformPosition(InOutViewLocation);

	// Initialize frustum
	IMPCDI::FFrustum& OutFrustum = Views[ViewIdx].Frustum;
	OutFrustum.OriginLocation = LocalOrigin;
	OutFrustum.OriginEyeOffset = LocalEyeOrigin - LocalOrigin;

	// Compute frustum
	if (MPCDIAPI.ComputeFrustum(WarpRef, WorldScale, NCP, FCP, OutFrustum))
	{
		OutFrustum.bIsValid = true;

		FMatrix OutPrjMatrix;
		GetProjectionMatrix(ViewIdx, OutPrjMatrix);

		// Get rotation in warp space
		const FRotator MpcdiRotation = Views[ViewIdx].Frustum.OutCameraRotation;
		const FVector  MpcdiOrigin = Views[ViewIdx].Frustum.OutCameraOrigin;

		InOutViewRotation = World2LocalTransform.TransformRotation(MpcdiRotation.Quaternion()).Rotator();
		InOutViewLocation = World2LocalTransform.TransformPosition(MpcdiOrigin);

		IPicpProjection::Get().SetViewport(GetViewportId(), InOutViewRotation, InOutViewLocation, OutPrjMatrix);

		if (CVarPicpShowFakeCamera.GetValueOnAnyThread() != 0)
		{
			//Add debug content for overlay shaders test purpose:
			FPicpProjectionOverlayFrameData::GenerateDebugContent(GetViewportId(), this);
		}

		return true;
	}

	OutFrustum.bIsValid = false;
	return false;
}

bool FPicpProjectionMPCDIPolicy::GetProjectionMatrix(const uint32 ViewIdx, FMatrix& OutPrjMatrix)
{
	check(IsInGameThread());

	if (Views[ViewIdx].Frustum.bIsValid)
	{
		OutPrjMatrix = Views[ViewIdx].Frustum.ProjectionMatrix;
		return true;
	}

	return false;
}

bool FPicpProjectionMPCDIPolicy::IsWarpBlendSupported()
{
	return true;
}

static bool GetPooledSceneRTT_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ViewportName, const uint32 ViewIdx, FIntPoint Size, EPixelFormat Format, bool bSRGB, TRefCountPtr<IPooledRenderTarget>& OutPooledPicpSceneRTT)
{
	FPooledRenderTargetDesc OutputDesc(FPooledRenderTargetDesc::Create2DDesc(Size, Format, FClearValueBinding::None, bSRGB? TexCreate_SRGB: TexCreate_None, TexCreate_RenderTargetable, false));
	GRenderTargetPool.FindFreeElement(RHICmdList, OutputDesc, OutPooledPicpSceneRTT, *FString::Printf(TEXT("nDisplayPicpSceneRTT_%s[%d]"), *ViewportName, ViewIdx));
	return OutPooledPicpSceneRTT.IsValid();
}

void FPicpProjectionMPCDIPolicy::ApplyWarpBlend_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect)
{
	check(IsInRenderingThread());

	FScopeLock lock(&WarpRefCS);
	if (!WarpRef.IsValid())
	{
		return;
	}

	bool bSRGB = ((SrcTexture->GetFlags() & TexCreate_SRGB) != 0);

	TRefCountPtr<IPooledRenderTarget> WarpRenderTargetPool;
	if (!GetPooledSceneRTT_RenderThread(RHICmdList, GetViewportId(), ViewIdx, GetViewportSize(), SrcTexture->GetFormat(), bSRGB, WarpRenderTargetPool))
	{
		// Error: Pooled RTT for scene not created
		return;
	}

	FTexture2DRHIRef RHIWarpRenderTarget = (const FTexture2DRHIRef&)WarpRenderTargetPool->GetRenderTargetItem().TargetableTexture;

	//Support runtime PFM reload
	MPCDIAPI.ReloadAll_RenderThread();
	
	FPicpProjectionOverlayViewportData ViewportOverlayData;
	GetOverlayData_RenderThread(ViewportOverlayData);
	AssignStageCamerasTextures_RenderThread(ViewportOverlayData);

	// Initialize shader input data
	IMPCDI::FShaderInputData ShaderInputData;
	ShaderInputData.RegionLocator = WarpRef;
	ShaderInputData.Frustum = Views[ViewIdx].Frustum;

	// Initialize texture data
	IMPCDI::FTextureWarpData TextureWarpData;
	TextureWarpData.SrcTexture = SrcTexture;
	TextureWarpData.SrcRect = ViewportRect;

	TextureWarpData.DstTexture = RHIWarpRenderTarget;
	TextureWarpData.DstRect = FIntRect(FIntPoint(0, 0), GetViewportSize());

	TSharedPtr<FMPCDIData> MPCDIData = MPCDIAPI.GetMPCDIData(ShaderInputData);
	if (!MPCDIData.IsValid())
	{
		UE_LOG(LogPicpProjectionMPCDI, Warning, TEXT("Couldn't apply warp&blend"));
		return;
	}
	
	//Execute composure
	IPicpMPCDI::Get().ExecuteCompose();

	// Perform warp&blend shader call (Picp module with data from base mpdi module)
	if (!PicpMPCDIAPI.ApplyWarpBlend(RHICmdList, TextureWarpData, ShaderInputData, MPCDIData.Get(), &ViewportOverlayData))
	{
		UE_LOG(LogPicpProjectionMPCDI, Warning, TEXT("Couldn't apply warp&blend"));
		return;
	}

	// Copy result back to the scene source
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

	RHICmdList.CopyToResolveTarget(RHIWarpRenderTarget, SrcTexture, copyParams);

	if (Views[ViewIdx].ExtWarpTexture != nullptr)
	{
		// Copy warp result back to ext texture (debug purpose)
		RHICmdList.CopyToResolveTarget(RHIWarpRenderTarget, Views[ViewIdx].ExtWarpTexture, copyParams);
		Views[ViewIdx].ExtWarpFrustum = Views[ViewIdx].Frustum; // Make snapshot of Frustum data for captured warp texture
		Views[ViewIdx].ExtWarpTexture = nullptr; // Stop capture right now
	}

	// Flush RHI commands before PooledPicpSceneRTT released
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
}

void FPicpProjectionMPCDIPolicy::SetWarpTextureCapture(const uint32 ViewIdx, FRHITexture2D* target)
{
	if ((int)ViewIdx < Views.Num())
	{
		if (target != 0)
		{
			Views[ViewIdx].ExtWarpTexture = target;
		}
		else
		{
			Views[ViewIdx].ExtWarpTexture = nullptr;
		}
	}
}

IMPCDI::FFrustum FPicpProjectionMPCDIPolicy::GetWarpFrustum(const uint32 ViewIdx, bool bIsCaptureWarpTextureFrustum)
{
	if ((int)ViewIdx < Views.Num())
	{
		if (bIsCaptureWarpTextureFrustum)
		{
			return Views[ViewIdx].ExtWarpFrustum;
		}

		return Views[ViewIdx].Frustum;
	}
	
	return IMPCDI::FFrustum();
}
