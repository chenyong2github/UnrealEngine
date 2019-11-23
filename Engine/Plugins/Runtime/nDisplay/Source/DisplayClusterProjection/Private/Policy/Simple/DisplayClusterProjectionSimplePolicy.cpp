// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Policy/Simple/DisplayClusterProjectionSimplePolicy.h"

#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"

#include "DisplayClusterUtils/DisplayClusterCommonHelpers.h"

#include "IDisplayCluster.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "Game/IDisplayClusterGameManager.h"

#include "DisplayClusterRootComponent.h"
#include "DisplayClusterScreenComponent.h"


FDisplayClusterProjectionSimplePolicy::FDisplayClusterProjectionSimplePolicy(const FString& ViewportId)
	: FDisplayClusterProjectionPolicyBase(ViewportId)
{
}

FDisplayClusterProjectionSimplePolicy::~FDisplayClusterProjectionSimplePolicy()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicy
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterProjectionSimplePolicy::StartScene(UWorld* World)
{
	check(IsInGameThread());

	// The game side of the nDisplay has been initialized by the nDisplay Game Manager already
	// so we can extend it by our projection related functionality/components/etc.
	InitializeMeshData();
}

void FDisplayClusterProjectionSimplePolicy::EndScene()
{
	check(IsInGameThread());

	// Forget about screen. It will be released by the Engine.
	ScreenComp = nullptr;
}

bool FDisplayClusterProjectionSimplePolicy::HandleAddViewport(const FIntPoint& ViewportSize, const uint32 ViewsAmount)
{
	check(IsInGameThread());

	UE_LOG(LogDisplayClusterProjectionSimple, Log, TEXT("Initializing internals for the viewport '%s'"), *GetViewportId());

	// Get projection settings of the specified viewport
	FDisplayClusterConfigProjection CfgProjection;
	if (!DisplayClusterHelpers::config::GetViewportProjection(GetViewportId(), CfgProjection))
	{
		UE_LOG(LogDisplayClusterProjectionSimple, Error, TEXT("No projection ID found for viewport '%s'"), *GetViewportId());
		return false;
	}

	// Get assigned screen ID
	FString ScreenId;
	if (!DisplayClusterHelpers::str::ExtractValue(CfgProjection.Params, DisplayClusterStrings::cfg::data::projection::simple::Screen, ScreenId))
	{
		UE_LOG(LogDisplayClusterProjectionSimple, Error, TEXT("No screen ID specified for projection '%s'"), *CfgProjection.Id);
		return false;
	}

	// Get config manager interface
	IDisplayClusterConfigManager* const ConfigMgr = IDisplayCluster::Get().GetConfigMgr();
	if (!ConfigMgr)
	{
		UE_LOG(LogDisplayClusterProjectionSimple, Error, TEXT("Couldn't get ConfigManager"));
		return false;
	}

	if (!ConfigMgr->GetScreen(ScreenId, CfgScreen))
	{
		UE_LOG(LogDisplayClusterProjectionSimple, Error, TEXT("Screen '%s' not found in the config file"), *ScreenId);
		return false;
	}

	UE_LOG(LogDisplayClusterProjectionSimple, Log, TEXT("Screen '%s' was mapped to the viewport '%s'"), *ScreenId, *GetViewportId());
	
	ViewData.Empty();
	ViewData.AddUninitialized(ViewsAmount);
	
	return true;
}

void FDisplayClusterProjectionSimplePolicy::HandleRemoveViewport()
{
	check(IsInGameThread());

	UE_LOG(LogDisplayClusterProjectionSimple, Log, TEXT("Removing viewport '%s'"), *GetViewportId());
	
	ReleaseMeshData();
}

bool FDisplayClusterProjectionSimplePolicy::CalculateView(const uint32 ViewIdx, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP)
{
	check(IsInGameThread());

	// Just store the data. We will compute the frustum later on request
	ViewData[ViewIdx].ViewLoc = InOutViewLocation;
	ViewData[ViewIdx].NCP = NCP;
	ViewData[ViewIdx].FCP = FCP;
	ViewData[ViewIdx].WorldToMeters = WorldToMeters;

	// View rotation is the same as screen's rotation (orthogonal to the screen plane)
	check(ScreenComp);
	InOutViewRotation = (ScreenComp ? ScreenComp->GetComponentRotation() : FRotator::ZeroRotator);

	return true;
}

bool FDisplayClusterProjectionSimplePolicy::GetProjectionMatrix(const uint32 ViewIdx, FMatrix& OutPrjMatrix)
{
	check(IsInGameThread());

	UE_LOG(LogDisplayClusterProjectionSimple, Verbose, TEXT("Calculating projection matrix for viewport '%s'..."), *GetViewportId());

	check(ViewData.Num() > int(ViewIdx))
	if (!ScreenComp)
	{
		UE_LOG(LogDisplayClusterProjectionSimple, Warning, TEXT("No screen found"));
		return false;
	}

	const float n = ViewData[ViewIdx].NCP;
	const float f = ViewData[ViewIdx].FCP;

	// Half-size
	const float hw = ScreenComp->GetScreenSize().X / 2.f * ViewData[ViewIdx].WorldToMeters;
	const float hh = ScreenComp->GetScreenSize().Y / 2.f * ViewData[ViewIdx].WorldToMeters;

	// Screen data
	const FVector  ScreenLoc = ScreenComp->GetComponentLocation();
	const FRotator ScreenRot = ScreenComp->GetComponentRotation();

	// Screen corners
	const FVector pa = ScreenLoc + ScreenRot.Quaternion().RotateVector(GetProjectionScreenGeometryLBC(hw, hh)); // left bottom corner
	const FVector pb = ScreenLoc + ScreenRot.Quaternion().RotateVector(GetProjectionScreenGeometryRBC(hw, hh)); // right bottom corner
	const FVector pc = ScreenLoc + ScreenRot.Quaternion().RotateVector(GetProjectionScreenGeometryLTC(hw, hh)); // left top corner

	// Screen vectors
	FVector vr = pb - pa; // lb->rb normalized vector, right axis of projection screen
	vr.Normalize();
	FVector vu = pc - pa; // lb->lt normalized vector, up axis of projection screen
	vu.Normalize();
	FVector vn = -FVector::CrossProduct(vr, vu); // Projection plane normal. Use minus because of left-handed coordinate system
	vn.Normalize();

	const FVector pe = ViewData[ViewIdx].ViewLoc; // ViewLocation;

	const FVector va = pa - pe; // camera -> lb
	const FVector vb = pb - pe; // camera -> rb
	const FVector vc = pc - pe; // camera -> lt

	const float d = -FVector::DotProduct(va, vn); // distance from eye to screen

	static const float minScreenDistance = 10; //Minimal distance from eye to screen
	const float SafeDistance = (fabs(d) < minScreenDistance) ? minScreenDistance : d;

	const float ndifd = n / SafeDistance;

	const float l = FVector::DotProduct(vr, va) * ndifd; // distance to left screen edge
	const float r = FVector::DotProduct(vr, vb) * ndifd; // distance to right screen edge
	const float b = FVector::DotProduct(vu, va) * ndifd; // distance to bottom screen edge
	const float t = FVector::DotProduct(vu, vc) * ndifd; // distance to top screen edge

	OutPrjMatrix = DisplayClusterHelpers::math::GetProjectionMatrixFromOffsets(l, r, t, b, n, f);

	return true;
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterProjectionSimplePolicy
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterProjectionSimplePolicy::InitializeMeshData()
{
	UE_LOG(LogDisplayClusterProjectionSimple, Log, TEXT("Initializing screen geometry for viewport %s"), *GetViewportId());

	// Get game manager interface
	const IDisplayClusterGameManager* const GameMgr = IDisplayCluster::Get().GetGameMgr();
	if (!GameMgr)
	{
		UE_LOG(LogDisplayClusterProjectionSimple, Error, TEXT("Couldn't get GameManager"));
		return;
	}

	// Get our VR root
	UDisplayClusterRootComponent* Root = GameMgr->GetRootComponent();
	if (!Root)
	{
		UE_LOG(LogDisplayClusterProjectionSimple, Error, TEXT("Couldn't get a VR root object"));
		return;
	}

	// Find a parent component for our new screen
	USceneComponent* ParentComp = nullptr;
	if (CfgScreen.ParentId.IsEmpty())
	{
		ParentComp = Root;
	}
	else
	{
		ParentComp = GameMgr->GetNodeById(CfgScreen.ParentId);
	}

	if (!ParentComp)
	{
		UE_LOG(LogDisplayClusterProjectionSimple, Warning, TEXT("Couldn't find a parent component for the new screen component"));
		return;
	}

	// Finally, create the component
	ScreenComp = NewObject<UDisplayClusterScreenComponent>(Root->GetOwner(), FName(*CfgScreen.Id), RF_Transient);
	check(ScreenComp);

	// Initialize it
	ScreenComp->AttachToComponent(ParentComp, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));
	ScreenComp->RegisterComponent();
	ScreenComp->SetSettings(&CfgScreen);
	ScreenComp->ApplySettings();

	UE_LOG(LogDisplayClusterProjectionSimple, Log, TEXT("Screen component '%s' has been attached to the component '%s'"), *ScreenComp->GetName(), *ParentComp->GetName());
}

void FDisplayClusterProjectionSimplePolicy::ReleaseMeshData()
{
	if (ScreenComp)
	{
		UE_LOG(LogDisplayClusterProjectionSimple, Log, TEXT("Removing screen component '%s'..."), *ScreenComp->GetName());
		
		// Destroy the component
		ScreenComp->DestroyComponent(true);
		ScreenComp = nullptr;
	}
}
