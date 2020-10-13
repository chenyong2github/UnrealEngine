// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/Simple/DisplayClusterProjectionSimplePolicy.h"

#include "IDisplayCluster.h"
#include "Game/IDisplayClusterGameManager.h"
#include "Misc/DisplayClusterHelpers.h"

#include "DisplayClusterRootActor.h"
#include "Components/DisplayClusterCameraComponent.h"
#include "Components/DisplayClusterScreenComponent.h"
#include "Components/MeshComponent.h"

#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"


FDisplayClusterProjectionSimplePolicy::FDisplayClusterProjectionSimplePolicy(const FString& ViewportId, const TMap<FString, FString>& Parameters)
	: FDisplayClusterProjectionPolicyBase(ViewportId, Parameters)
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

	// Get assigned screen ID
	if (!DisplayClusterHelpers::map::template ExtractValue(GetParameters(), DisplayClusterProjectionStrings::cfg::simple::Screen, ScreenId))
	{
		UE_LOG(LogDisplayClusterProjectionSimple, Error, TEXT("No screen ID specified for projection policy of viewport '%s'"), *GetViewportId());
		return false;
	}

	ViewData.Empty();
	ViewData.AddDefaulted(ViewsAmount);
	
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
	const float hw = ScreenComp->GetScreenSize().X / 2.f / 100.f * ViewData[ViewIdx].WorldToMeters;
	const float hh = ScreenComp->GetScreenSize().Y / 2.f / 100.f * ViewData[ViewIdx].WorldToMeters;

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
	ADisplayClusterRootActor* const Root = GameMgr->GetRootActor();
	if (!Root)
	{
		UE_LOG(LogDisplayClusterProjectionSimple, Error, TEXT("Couldn't get a VR root object"));
		return;
	}

	// Get screen component
	ScreenComp = Root->GetScreenById(ScreenId);
	if (!ScreenComp)
	{
		UE_LOG(LogDisplayClusterProjectionSimple, Warning, TEXT("Couldn't initialize screen component"));
		return;
	}
}

void FDisplayClusterProjectionSimplePolicy::ReleaseMeshData()
{
}
