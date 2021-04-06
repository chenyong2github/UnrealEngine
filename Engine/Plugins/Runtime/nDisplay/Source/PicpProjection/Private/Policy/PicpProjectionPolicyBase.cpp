// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/PicpProjectionPolicyBase.h"

#include "PicpProjectionLog.h"

#include "IDisplayCluster.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "Game/IDisplayClusterGameManager.h"

#include "DisplayClusterRootActor.h"
#include "Components/DisplayClusterSceneComponent.h"

#include "Overlay/PicpProjectionOverlayRender.h"

#include "IDisplayCluster.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "Game/IDisplayClusterGameManager.h"
#include "Misc/DisplayClusterHelpers.h"
#include "Render/IDisplayClusterRenderManager.h"
#include "Render/Device/IDisplayClusterRenderDevice.h"

FPicpProjectionPolicyBase::FPicpProjectionPolicyBase(FPicpProjectionModule& InPicpProjectionModule, const FString& InViewportId, const TMap<FString, FString>& InParameters)
	: PolicyViewportId(InViewportId)
	, Parameters(InParameters)
	, PicpProjectionModule(InPicpProjectionModule)
{
}

FPicpProjectionPolicyBase::~FPicpProjectionPolicyBase()
{
}

void FPicpProjectionPolicyBase::SetViewportSize(const FIntPoint& InViewportSize)
{
	ViewportSize = InViewportSize;
}

void FPicpProjectionPolicyBase::AssignStageCamerasTextures_RenderThread(FPicpProjectionOverlayViewportData& InOutViewportOverlayData)
{
	for (auto& It : InOutViewportOverlayData.Cameras)
	{
		TSharedPtr<FPicpProjectionStageCameraResource> StageCameraResource;
		if (PicpProjectionModule.FindOrAddStageCameraResource(It.RTTViewportId, StageCameraResource))
		{
			It.CameraTexture = StageCameraResource->GetStageCameraTexture();
			check(It.CameraTexture);
		}
	}
}

void FPicpProjectionPolicyBase::EndWarpBlend_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect)
{
	// Release all pooled stage cameras RTT after warpblend done
	FPicpProjectionOverlayViewportData ViewportOverlayData;
	GetOverlayData_RenderThread(ViewportOverlayData);
	for (auto& It : ViewportOverlayData.Cameras)
	{
		TSharedPtr<FPicpProjectionStageCameraResource> StageCameraResource;
		if (PicpProjectionModule.FindOrAddStageCameraResource(It.RTTViewportId, StageCameraResource))
		{
			StageCameraResource->DiscardStageCameraRTT();
		}
	}
}

void FPicpProjectionPolicyBase::BeginWarpBlend_RenderThread(const uint32 ViewIdx, FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, const FIntRect& ViewportRect)
{
	// Resize viewport runtime
	SetViewportSize(ViewportRect.Size());

	// Initialize all stage cameras
	FPicpProjectionOverlayViewportData ViewportOverlayData;
	GetOverlayData_RenderThread(ViewportOverlayData);

	// Create and allocate RTTs pool for all stage cameras
	IDisplayClusterRenderManager* const Manager = IDisplayCluster::Get().GetRenderMgr();
	check(Manager);

	IDisplayClusterRenderDevice* const RenderDevice = Manager->GetRenderDevice();
	check(RenderDevice);

	//Copy all cameras RTT render to separate textures
	for (const auto& It : ViewportOverlayData.Cameras)
	{
		FPicpProjectionStageCameraResource::FCameraData CameraData;
		CameraData.ViewportId = It.RTTViewportId;

		if (!CameraData.ViewportId.IsEmpty())
		{
			if (RenderDevice->GetViewportRect(CameraData.ViewportId, CameraData.ViewportRect))
			{
				// Found stage camera, initialize
				CameraData.Format = SrcTexture->GetFormat();
				if ((SrcTexture->GetFlags() & TexCreate_SRGB) != 0)
				{
					CameraData.bSRGB = true;
				}

				CameraData.NumMips = It.NumMips;
				CameraData.CustomCameraTexture = It.CustomCameraTexture;

				// Setup mips, etc ...
				TSharedPtr<FPicpProjectionStageCameraResource> StageCameraResource;
				if (PicpProjectionModule.FindOrAddStageCameraResource(CameraData.ViewportId, StageCameraResource))
				{
					// temp logic, add renderer inside latter 
					StageCameraResource->InitializeStageCameraRTT(RHICmdList, CameraData);
					StageCameraResource->UpdateStageCameraRTT(RHICmdList, SrcTexture);
				}
			}
			else
			{
				//@todo: handle error, invalid viewport name
			}
		}
	}
}

void FPicpProjectionPolicyBase::InitializeOriginComponent(const FString& OriginCompId)
{
	UE_LOG(LogPicpProjectionMPCDI, Log, TEXT("Looking for an origin component '%s'..."), *OriginCompId);

	// Reset previous one
	PolicyOriginComponentRef.ResetSceneComponent();

	IDisplayClusterGameManager* const GameMgr = IDisplayCluster::Get().GetGameMgr();
	if (!GameMgr)
	{
		UE_LOG(LogPicpProjectionMPCDI, Warning, TEXT("No DisplayCluster game manager available"));
		return;
	}

	USceneComponent* PolicyOriginComp = nullptr;
	ADisplayClusterRootActor* const RootActor = GameMgr->GetRootActor();
	if (RootActor)
	{
		// Try to get a node specified in the config file
		if (!OriginCompId.IsEmpty())
		{
			PolicyOriginComp = RootActor->GetComponentById(OriginCompId);
		}

		// If no origin component found, use the root component as the origin
		if (PolicyOriginComp == nullptr)
		{
			UE_LOG(LogPicpProjectionMPCDI, Log, TEXT("No custom origin set or component '%s' not found for viewport '%s'. VR root will be used."), *OriginCompId, *PolicyViewportId);
			PolicyOriginComp = RootActor->GetRootComponent();
		}
	}

	if (!PolicyOriginComp)
	{
		PolicyOriginComponentRef.ResetSceneComponent();
		UE_LOG(LogPicpProjectionMPCDI, Error, TEXT("Couldn't set origin component"));
		return;
	}

	PolicyOriginComponentRef.SetSceneComponent(PolicyOriginComp);
}

void FPicpProjectionPolicyBase::ReleaseOriginComponent()
{
	PolicyOriginComponentRef.ResetSceneComponent();
}


void FPicpProjectionPolicyBase::UpdateOverlayViewportData(FPicpProjectionOverlayFrameData& OverlayFrameData)
{
	// Transform rotation to world space
	const USceneComponent* const OriginComp = GetOriginComp();
	const FTransform& World2LocalTransform = (OriginComp ? OriginComp->GetComponentTransform() : FTransform::Identity);

	OverlayFrameData.GetViewportData(GetViewportId(), this, World2LocalTransform);
}

void FPicpProjectionPolicyBase::SetOverlayData_RenderThread(const FPicpProjectionOverlayViewportData* Source)
{
	check(IsInRenderingThread());

	if (Source)
	{
		FScopeLock lock(&LocalOverlayViewportDataCS);
		LocalOverlayViewportData.Initialize(*Source); // Copy data on render thread
	}
}

void FPicpProjectionPolicyBase::GetOverlayData_RenderThread(FPicpProjectionOverlayViewportData& Output)
{
	check(IsInRenderingThread());

	FScopeLock lock(&LocalOverlayViewportDataCS);
	Output.Initialize(LocalOverlayViewportData);
}


void FPicpProjectionOverlayViewportData::Initialize(const FPicpProjectionOverlayViewportData& Source)
{
	// Clear prev data immediately
	Empty();

	LUTCorrection = Source.LUTCorrection;
	ViewportOver = Source.ViewportOver;
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

static void SetOverlayData_RenderThread(FRHICommandListImmediate& RHICmdList, FPicpProjectionPolicyBase* Policy, FPicpProjectionOverlayViewportData* Source)
{
	Policy->SetOverlayData_RenderThread(Source);
	// Remove used input data at the end of thread
	if (Source)
	{
		delete Source;
		Source = nullptr;
	}
}

void FPicpProjectionOverlayFrameData::GetViewportData(const FString& ViewportId, FPicpProjectionPolicyBase* OutPolicy, const FTransform& Origin2WorldTransform) const
{
	// Create input data for render thread (will be deleted inside render thread)
	FPicpProjectionOverlayViewportData* OverlayData_RenderThread = new FPicpProjectionOverlayViewportData();

	// Copy LUT data:
	OverlayData_RenderThread->LUTCorrection = LUTCorrection;

	const bool bHideCamerasForThisViewport = ViewportsWithoutIncamera.ContainsByPredicate([ViewportId](const FString& HideViewportId)
	{
		return ViewportId.Equals(HideViewportId, ESearchCase::IgnoreCase);
	});

	if (!bHideCamerasForThisViewport)
	{
		// Copy cameras data:
		for (const auto& It : Cameras)
		{
			FPicpProjectionOverlayCamera Camera(It);

			// Transform camera view matrix to cave space
			Camera.ViewRot = Origin2WorldTransform.InverseTransformRotation(Camera.ViewRot.Quaternion()).Rotator();
			Camera.ViewLoc = Origin2WorldTransform.InverseTransformPosition(Camera.ViewLoc);

			OverlayData_RenderThread->Cameras.Add(Camera);
		}
	}

	// Copy overlays data:
	if (ViewportsOver.Contains(ViewportId))
	{
		OverlayData_RenderThread->ViewportOver = ViewportsOver[ViewportId];
	}

	if (ViewportsUnder.Contains(ViewportId))
	{
		OverlayData_RenderThread->ViewportUnder = ViewportsUnder[ViewportId];
	}

	//Send data to render thread:
	ENQUEUE_RENDER_COMMAND(SetOverlayData_RenderThread)(
		[OutPolicy, OverlayData_RenderThread](FRHICommandListImmediate& RHICmdList)
	{
		SetOverlayData_RenderThread(RHICmdList, OutPolicy, OverlayData_RenderThread);
	});
}

void FPicpProjectionOverlayFrameData::Empty()
{
	ViewportsOver.Empty();
	ViewportsUnder.Empty();
	Cameras.Empty();
}

FMatrix GetSimpleProjectionMatrix(float Fov, float ZNear, float ZFar)
{
	const float r = Fov / 2;
	const float l = -r;
	const float t = Fov / 2;
	const float b = -t;

	return DisplayClusterHelpers::math::GetProjectionMatrixFromAngles(l, r, t, b, ZNear, ZFar);
}

void FPicpProjectionOverlayFrameData::GenerateDebugContent(const FString& ViewportId, FPicpProjectionPolicyBase* OutPolicy)
{
	float Yaw = 45; // Floor rotation
	float Fov = 10;

	FPicpProjectionOverlayCamera DebugCamera(
		FRotator(0, Yaw, 0),
		FVector(0, 0, 0),
		GetSimpleProjectionMatrix(Fov, 1, 20000)
		, TEXT("DebugInnerCameraViewport"));

	FPicpProjectionOverlayViewportData* GameThreadData = new FPicpProjectionOverlayViewportData();
	GameThreadData->Cameras.Add(DebugCamera);

	//Send data to render thread:
	ENQUEUE_RENDER_COMMAND(SetOverlayData_RenderThread)(
		[OutPolicy, GameThreadData](FRHICommandListImmediate& RHICmdList)
	{
		SetOverlayData_RenderThread(RHICmdList, OutPolicy, GameThreadData);
	});
}
