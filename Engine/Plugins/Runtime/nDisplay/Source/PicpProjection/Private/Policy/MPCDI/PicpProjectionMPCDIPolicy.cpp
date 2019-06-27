// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "Policy/MPCDI/PicpProjectionMPCDIPolicy.h"

#include "PicpProjectionHelpers.h"
#include "PicpProjectionLog.h"
#include "PicpProjectionStrings.h"

#include "DisplayClusterProjectionHelpers.h"
#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"

#include "Config/DisplayClusterConfigTypes.h"
#include "Game/IDisplayClusterGameManager.h"

#include "XRThreadUtils.h"

#include "Engine/RendererSettings.h"
#include "Misc/Paths.h"


static TAutoConsoleVariable<int32> CVarPicpShowFakeCamera(
	TEXT("nDisplay.render.picp.ShowFakeCamera"),
	0,
	TEXT("Show fake debug camera (0 = disabled)"),
	ECVF_RenderThreadSafe
);


FPicpProjectionMPCDIPolicy::FPicpProjectionMPCDIPolicy(const FString& ViewportId)
	: FPicpProjectionPolicyBase(ViewportId)
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
}

bool FPicpProjectionMPCDIPolicy::HandleAddViewport(const FIntPoint& InViewportSize, const uint32 InViewsAmount)
{
	check(IsInGameThread());
	check(InViewsAmount > 0);

	FString File;
	FString Buffer;
	FString Region;

	// Read MPCDI config data from nDisplay config file
	if (!ReadConfigData(GetViewportId(), File, Buffer, Region, OriginCompId))
	{
		UE_LOG(LogPicpProjectionMPCDI, Error, TEXT("Couldn't read MPCDI configuration from the config file"));
		return false;
	}

	// Check if MPCDI file exists
	if (!FPaths::FileExists(File))
	{
		UE_LOG(LogPicpProjectionMPCDI, Warning, TEXT("File not found: %s"), *File);
		return false;
	}

	// Load MPCDI file
	if (!MPCDIAPI.Load(File))
	{
		UE_LOG(LogPicpProjectionMPCDI, Warning, TEXT("Couldn't load MPCDI file: %s"), *File);
		return false;
	}

	// Store MPCDI region locator for this viewport
	if (!MPCDIAPI.GetRegionLocator(File, Buffer, Region, WarpRef))
	{
		UE_LOG(LogPicpProjectionMPCDI, Warning, TEXT("Couldn't get region locator for <buf %s, reg %s> in file: %s # %s "), *Buffer, *Region, *File);
		return false;
	}

	UE_LOG(LogPicpProjectionMPCDI, Log, TEXT("MPCDI policy has been initialized [%s:%s in %s]"), *Buffer, *Region, *File);

	// Finally, initialize internal views data container
	Views.AddDefaulted(InViewsAmount);
	ViewportSize = InViewportSize;

	return true;
}

void FPicpProjectionMPCDIPolicy::HandleRemoveViewport()
{
	check(IsInGameThread());
}

bool FPicpProjectionMPCDIPolicy::CalculateView(const uint32 ViewIdx, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP)
{
	check(IsInGameThread());

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

	if (Views[ViewIdx].RTTexture != nullptr)
	{
		OutFrustum.ViewportSize = Views[ViewIdx].RTTexture->GetSizeXY();
	}

	// Compute frustum
	if (MPCDIAPI.ComputeFrustum(WarpRef, WorldScale, NCP, FCP, OutFrustum))
	{
		OutFrustum.bIsValid = true;

		FMatrix OutPrjMatrix;
		GetProjectionMatrix(ViewIdx, OutPrjMatrix);

		// Get rotation in warp space
		const FRotator MpcdiRotation = Views[ViewIdx].Frustum.Local2WorldMatrix.Rotator();
		const FVector  MpcdiOrigin   = Views[ViewIdx].Frustum.Local2WorldMatrix.GetOrigin();

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

	return false;
}

bool FPicpProjectionMPCDIPolicy::GetProjectionMatrix(const uint32 ViewIdx, FMatrix& OutPrjMatrix)
{
	check(IsInGameThread());

	OutPrjMatrix = Views[ViewIdx].Frustum.ProjectionMatrix;

	return true;
}

bool FPicpProjectionMPCDIPolicy::IsWarpBlendSupported()
{
	return true;
}

void FPicpProjectionMPCDIPolicy::ApplyWarpBlend_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect)
{
	check(IsInRenderingThread());

	if (!InitializeResources_RenderThread())
	{
		UE_LOG(LogPicpProjectionMPCDI, Warning, TEXT("Couldn't initialize rendering resources"));
		return;
	}

	FPicpProjectionOverlayViewportData ViewportOverlayData;
	ViewportOverlayData.Initialize(OverlayViewportData);

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

	TSharedPtr<FMPCDIData> MPCDIData = MPCDIAPI.GetMPCDIData(ShaderInputData);
	if (!MPCDIData.IsValid())
	{
		UE_LOG(LogPicpProjectionMPCDI, Warning, TEXT("Couldn't apply warp&blend"));
		return;
	}

	// Perform warp&blend shader call (Picp module with data from base mpdi module)
	if (!PicpMPCDIAPI.ApplyWarpBlend(RHICmdList, TextureWarpData, ShaderInputData, MPCDIData.Get(), &ViewportOverlayData))
	{
		UE_LOG(LogPicpProjectionMPCDI, Warning, TEXT("Couldn't apply warp&blend"));
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

	if (Views[ViewIdx].ExtWarpTexture!=nullptr)
	{
		RHICmdList.CopyToResolveTarget(Views[ViewIdx].RTTexture, Views[ViewIdx].ExtWarpTexture, copyParams);
		Views[ViewIdx].ExtWarpFrustum = Views[ViewIdx].Frustum; // Make snapshot of Frustum data for captured warp texture
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterProjectionMPCDIPolicy
//////////////////////////////////////////////////////////////////////////////////////////////
bool FPicpProjectionMPCDIPolicy::ReadConfigData(const FString& ViewportId, FString& OutFile, FString& OutBuffer, FString& OutRegion, FString& OutOrigin)
{
	// Get projection settings of the specified viewport
	FDisplayClusterConfigProjection CfgProjection;
	if (!DisplayClusterHelpers::config::GetViewportProjection(ViewportId, CfgProjection))
	{
		UE_LOG(LogPicpProjectionMPCDI, Error, TEXT("Couldn't obtain projection info for '%s' viewport"), *ViewportId);
		return false;
	}

	// MPCDI file
	if (!DisplayClusterHelpers::str::ExtractValue(CfgProjection.Params, PicpProjectionStrings::cfg::data::projection::mpcdi::File, OutFile))
	{
		UE_LOG(LogPicpProjectionMPCDI, Error, TEXT("Argument '%s' not found in the config file"), PicpProjectionStrings::cfg::data::projection::mpcdi::File);
		return false;
	}

	// Buffer
	if (!DisplayClusterHelpers::str::ExtractValue(CfgProjection.Params, PicpProjectionStrings::cfg::data::projection::mpcdi::Buffer, OutBuffer))
	{
		UE_LOG(LogPicpProjectionMPCDI, Error, TEXT("Argument '%s' not found in the config file"), PicpProjectionStrings::cfg::data::projection::mpcdi::Buffer);
		return false;
	}

	// Region
	if (!DisplayClusterHelpers::str::ExtractValue(CfgProjection.Params, PicpProjectionStrings::cfg::data::projection::mpcdi::Region, OutRegion))
	{
		UE_LOG(LogPicpProjectionMPCDI, Error, TEXT("Argument '%s' not found in the config file"), PicpProjectionStrings::cfg::data::projection::mpcdi::Region);
		return false;
	}

	// Origin node (optional)
	if (DisplayClusterHelpers::str::ExtractValue(CfgProjection.Params, PicpProjectionStrings::cfg::data::projection::mpcdi::Origin, OutOrigin))
	{
		UE_LOG(LogPicpProjectionMPCDI, Log, TEXT("Found origin node for %s:%s - %s"), *OutBuffer, *OutRegion, *OutOrigin);
	}
	else
	{
		UE_LOG(LogPicpProjectionMPCDI, Log, TEXT("No origin node found for %s:%s. VR root will be used as default."), *OutBuffer, *OutRegion);
	}

	return true;
}

bool FPicpProjectionMPCDIPolicy::InitializeResources_RenderThread()
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
		}
	}

	return true;
}


//Picp extands api:
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

void FPicpProjectionMPCDIPolicy::UpdateOverlayViewportData(FPicpProjectionOverlayFrameData& OverlayFrameData)
{
	// Transform rotation to world space
	const USceneComponent* const OriginComp = GetOriginComp();
	const FTransform& World2LocalTransform = (OriginComp ? OriginComp->GetComponentTransform() : FTransform::Identity);

	OverlayFrameData.GetViewportData(GetViewportId(), this, World2LocalTransform);
}


void FPicpProjectionMPCDIPolicy::SetOverlayData_RenderThread(const FPicpProjectionOverlayViewportData* Source)
{
	check(IsInRenderingThread());

	if (Source)
	{
		OverlayViewportData.Initialize(*Source); // Copy data on render thread
	}
}


void FPicpProjectionOverlayViewportData::Initialize(const FPicpProjectionOverlayViewportData& Source)
{
	Empty(); //Clear prev data immediatelly

	if (Source.LUTCorrection)
	{
		LUTCorrection = new FPicpProjectionOverlayLUT(*Source.LUTCorrection);
	}

	if (Source.ViewportOver)
	{
		ViewportOver = new FPicpProjectionOverlayViewport(*Source.ViewportOver);
	}

	if (Source.ViewportUnder)
	{
		ViewportUnder = new FPicpProjectionOverlayViewport(*Source.ViewportUnder);
	}

	for (auto It : Source.Cameras)
	{
		Cameras.Add(new FPicpProjectionOverlayCamera(*It));
	}
}


void FPicpProjectionOverlayViewportData::Empty()
{
	check(IsInRenderingThread());

	if (LUTCorrection)
	{
		delete LUTCorrection;
		LUTCorrection = nullptr;
	}

	if (ViewportOver)
	{
		delete ViewportOver;
		ViewportOver = nullptr;
	}

	if (ViewportUnder)
	{
		delete ViewportUnder;
		ViewportUnder = nullptr;
	}

	for (auto& It : Cameras)
	{
		delete It;
	}

	Cameras.Empty();
}


namespace // Helpers
{
	FMatrix GetProjectionMatrixAssymetric(float l, float r, float t, float b, float n, float f)
	{
		static const FMatrix FlipZAxisToUE4 = FMatrix(
			FPlane(1, 0, 0, 0),
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, -1, 0),
			FPlane(0, 0, 1, 1));

		const float mx = 2.f * n / (r - l);
		const float my = 2.f * n / (t - b);
		const float ma = -(r + l) / (r - l);
		const float mb = -(t + b) / (t - b);
		const float mc = f / (f - n);
		const float md = -(f * n) / (f - n);
		const float me = 1.f;

		// Normal LHS
		FMatrix ProjectionMatrix = FMatrix(
			FPlane(mx, 0, 0, 0),
			FPlane(0, my, 0, 0),
			FPlane(ma, mb, mc, me),
			FPlane(0, 0, md, 0));

		return ProjectionMatrix * FlipZAxisToUE4;
	}

	template<class T>
	static T degToRad(T degrees)
	{
		return degrees * (T)(PI / 180.0);
	}

	FMatrix GetProjectionMatrix(float Fov, float ZNear, float ZFar)
	{
		float l = float(ZNear*tan(degToRad(-Fov / 2)));
		float r = float(ZNear*tan(degToRad(Fov / 2)));
		float b = float(ZNear*tan(degToRad(-Fov / 2)));
		float t = float(ZNear*tan(degToRad(Fov / 2)));

		return GetProjectionMatrixAssymetric(l, r, t, b, ZNear, ZFar);
	}
};


//#include "ShaderParameterUtils.h"
#include "Engine/TextureRenderTarget2D.h"

static void SetOverlayData_RenderThread(FRHICommandListImmediate& RHICmdList, FPicpProjectionMPCDIPolicy* Policy, FPicpProjectionOverlayViewportData* Source)
{
	Policy->SetOverlayData_RenderThread(Source);
	if (Source)
	{
		delete Source;
		Source = nullptr;
	}
}

void FPicpProjectionOverlayFrameData::Empty()
{
	if (LUTCorrection)
	{
		delete LUTCorrection;
		LUTCorrection = nullptr;
	}

	for (auto& It : ViewportsOver)
	{
		delete It.Value;
	}

	for (auto& It : ViewportsUnder)
	{
		delete It.Value;
	}

	for (auto& It : Cameras)
	{
		delete It;
	}

	ViewportsOver.Empty();
	ViewportsUnder.Empty();
	Cameras.Empty();
}

void FPicpProjectionOverlayFrameData::GetViewportData(const FString& ViewportId, FPicpProjectionMPCDIPolicy* OutPolicy, const FTransform& Origin2WorldTransform) const
{
	// Also transofrm to cave space

	FPicpProjectionOverlayViewportData* GameThreadData = new FPicpProjectionOverlayViewportData();

	// Check TSharedPtr counters
	if (LUTCorrection)
	{
		GameThreadData->LUTCorrection = new FPicpProjectionOverlayLUT(*LUTCorrection);
	}

	for (auto It : Cameras) {

		FPicpProjectionOverlayCamera* Cam = new FPicpProjectionOverlayCamera(*It);

		// Update view matrix to cave space
		Cam->ViewRot = Origin2WorldTransform.InverseTransformRotation(It->ViewRot.Quaternion()).Rotator();
		Cam->ViewLoc = Origin2WorldTransform.InverseTransformPosition(It->ViewLoc);

		GameThreadData->Cameras.Add(Cam);
	}

	if (ViewportsOver.Contains(ViewportId))
	{
		GameThreadData->ViewportOver = new FPicpProjectionOverlayViewport(*ViewportsOver[ViewportId]);
	}

	if (ViewportsUnder.Contains(ViewportId))
	{
		GameThreadData->ViewportUnder = new FPicpProjectionOverlayViewport(*ViewportsUnder[ViewportId]);
	}

	//Send data to render thread:
	ENQUEUE_RENDER_COMMAND(SetOverlayData_RenderThread)(
		[OutPolicy, GameThreadData](FRHICommandListImmediate& RHICmdList)
	{
		SetOverlayData_RenderThread(RHICmdList, OutPolicy, GameThreadData);
	}
	);
}

void FPicpProjectionOverlayFrameData::GenerateDebugContent(const FString& ViewportId, FPicpProjectionMPCDIPolicy* OutPolicy)
{
	float Yaw = 45; // Floor rotation
	float Fov = 10;

	FPicpProjectionOverlayCamera* DebugCamera = new FPicpProjectionOverlayCamera(
		FRotator(0, Yaw, 0),FVector(0,0,0),
		GetProjectionMatrix(Fov, 1, 20000), GWhiteTexture->TextureRHI);

	FPicpProjectionOverlayViewportData* GameThreadData = new FPicpProjectionOverlayViewportData();
	GameThreadData->Cameras.Add(DebugCamera);

	//Send data to render thread:
	ENQUEUE_RENDER_COMMAND(SetOverlayData_RenderThread)(
		[OutPolicy, GameThreadData](FRHICommandListImmediate& RHICmdList)
	{
		SetOverlayData_RenderThread(RHICmdList, OutPolicy, GameThreadData);
	}
	);
}
