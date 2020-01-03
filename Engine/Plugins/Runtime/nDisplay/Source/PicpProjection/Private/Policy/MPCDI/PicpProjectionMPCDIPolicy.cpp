// Copyright Epic Games, Inc. All Rights Reserved.


#include "Policy/MPCDI/PicpProjectionMPCDIPolicy.h"

#include "PicpProjectionHelpers.h"
#include "PicpProjectionLog.h"
#include "PicpProjectionStrings.h"

#include "DisplayClusterProjectionHelpers.h"
#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"

#include "Config/DisplayClusterConfigTypes.h"
#include "Game/IDisplayClusterGameManager.h"
#include "Render/IDisplayClusterRenderManager.h"

#include "Engine/RendererSettings.h"
#include "Misc/Paths.h"


static TAutoConsoleVariable<int32> CVarPicpShowFakeCamera(
	TEXT("nDisplay.render.picp.ShowFakeCamera"),
	0,
	TEXT("Show fake debug camera\n")
	TEXT(" 0: disabled\n")
	TEXT(" 1: enabled\n"),
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


	IMPCDI::ConfigParser CfgData;
	{
		//Load config:
		FDisplayClusterConfigProjection CfgProjection;
		if (!DisplayClusterHelpers::config::GetViewportProjection(GetViewportId(), CfgProjection))
		{
			UE_LOG(LogPicpProjectionMPCDI, Error, TEXT("Couldn't obtain projection info for '%s' viewport"), *GetViewportId());
			return false;
		}

		if (!MPCDIAPI.LoadConfig(CfgProjection.Params, CfgData))
		{
			UE_LOG(LogPicpProjectionMPCDI, Error, TEXT("Couldn't read MPCDI configuration from the config file"));
			return false;
		}
	}


	// Load MPCDI config
	if (!MPCDIAPI.Load(CfgData, WarpRef))
	{
		UE_LOG(LogPicpProjectionMPCDI, Warning, TEXT("Couldn't load PICP MPCDI config: %s"), *CfgData.ConfigLineStr);
		return false;
	}
	
	UE_LOG(LogPicpProjectionMPCDI, Log, TEXT("PICP MPCDI policy has been initialized [%s]"), *CfgData.ConfigLineStr);

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

void FPicpProjectionMPCDIPolicy::ApplyWarpBlend_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect)
{
	check(IsInRenderingThread());

	if (!InitializeResources_RenderThread())
	{
		UE_LOG(LogPicpProjectionMPCDI, Warning, TEXT("Couldn't initialize rendering resources"));
		return;
	
	}

	//Support runtime PFM reload
	MPCDIAPI.ReloadAll_RenderThread();
	
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
	
	// Copy rtt viewport into camera texture RTT
	IDisplayClusterRenderManager* const Manager = IDisplayCluster::Get().GetRenderMgr();
	if (Manager)
	{
		FIntRect RTTViewportRect;
		if (ViewportOverlayData.Cameras.Num() > 0)
		{
			bool RTTViewportFound = Manager->GetViewportRect(ViewportOverlayData.Cameras[0].RTTViewportId, RTTViewportRect);
			if (RTTViewportFound)
			{
				FResolveParams RTTViewportCopyParams;
				RTTViewportCopyParams.DestArrayIndex = 0;
				RTTViewportCopyParams.SourceArrayIndex = 0;

				RTTViewportCopyParams.Rect.X1 = RTTViewportRect.Min.X;
				RTTViewportCopyParams.Rect.X2 = RTTViewportRect.Max.X;
				RTTViewportCopyParams.Rect.Y1 = RTTViewportRect.Min.Y;
				RTTViewportCopyParams.Rect.Y2 = RTTViewportRect.Max.Y;

				RTTViewportCopyParams.DestRect.X1 = 0;
				RTTViewportCopyParams.DestRect.Y1 = 0;
				RTTViewportCopyParams.DestRect.X2 = ViewportOverlayData.Cameras[0].CameraTexture->GetSizeXYZ().X;
				RTTViewportCopyParams.DestRect.Y2 = ViewportOverlayData.Cameras[0].CameraTexture->GetSizeXYZ().Y;

				RHICmdList.CopyToResolveTarget(SrcTexture, ViewportOverlayData.Cameras[0].CameraTexture, RTTViewportCopyParams);
			}
			else
			{
				UE_LOG(LogPicpProjectionMPCDI, Error, TEXT("PICP Policy: RTT Viewport is not set. Assign in PICPOverlayFrameBlendingParameters."));
			}
		}
	}

	//Execute composure
	IPicpMPCDI::Get().ExecuteCompose();

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
		Views[ViewIdx].ExtWarpTexture = nullptr; // Stop capture right now
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterProjectionMPCDIPolicy
//////////////////////////////////////////////////////////////////////////////////////////////
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

			bIsRenderResourcesInitialized = true;
		}
	}

	return bIsRenderResourcesInitialized;
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

	LUTCorrection  = Source.LUTCorrection;
	 ViewportOver  = Source.ViewportOver;
	 ViewportUnder = Source.ViewportUnder;

	for (const auto& It : Source.Cameras)
	{
		Cameras.Add(It);
	}
}


void FPicpProjectionOverlayViewportData::Empty()
{
	check(IsInRenderingThread());

	LUTCorrection.Empty();
	ViewportOver.Empty();
	ViewportUnder.Empty();

	for (auto& It : Cameras)
	{
		It.Empty();
	}
	
	Cameras.Empty();
}


namespace // Helpers
{
	FMatrix GetProjectionMatrix(float Fov, float ZNear, float ZFar)
	{
		const float r = Fov / 2;
		const float l = -r;
		const float t = Fov / 2;
		const float b = -t;

		return DisplayClusterHelpers::math::GetProjectionMatrixFromAngles(l, r, t, b, ZNear, ZFar);
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
	ViewportsOver.Empty();
	ViewportsUnder.Empty();
	Cameras.Empty();
}

void FPicpProjectionOverlayFrameData::GetViewportData(const FString& ViewportId, FPicpProjectionMPCDIPolicy* OutPolicy, const FTransform& Origin2WorldTransform) const
{
	// Also transofrm to cave space

	FPicpProjectionOverlayViewportData* GameThreadData = new FPicpProjectionOverlayViewportData();

	GameThreadData->LUTCorrection = LUTCorrection;

	// Copy cameras data
	for (const auto& It : Cameras) {

		FPicpProjectionOverlayCamera Camera(It);

		// Update view matrix to cave space
		Camera.ViewRot = Origin2WorldTransform.InverseTransformRotation(Camera.ViewRot.Quaternion()).Rotator();
		Camera.ViewLoc = Origin2WorldTransform.InverseTransformPosition(Camera.ViewLoc);

		GameThreadData->Cameras.Add(Camera);
	}

	if (ViewportsOver.Contains(ViewportId))
	{
		GameThreadData->ViewportOver = ViewportsOver[ViewportId];
	}

	if (ViewportsUnder.Contains(ViewportId))
	{
		GameThreadData->ViewportUnder = ViewportsUnder[ViewportId];
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

	FPicpProjectionOverlayCamera DebugCamera(
		FRotator(0, Yaw, 0),FVector(0,0,0),
		GetProjectionMatrix(Fov, 1, 20000), GWhiteTexture->TextureRHI, TEXT("DebugInnerCameraViewport"));

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
