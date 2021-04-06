// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/Mesh/DisplayClusterProjectionMeshPolicy.h"
#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"
#include "Misc/DisplayClusterHelpers.h"


FDisplayClusterProjectionMeshPolicy::FDisplayClusterProjectionMeshPolicy(const FString& ViewportId, const TMap<FString, FString>& Parameters)
	: FDisplayClusterProjectionMPCDIPolicy(ViewportId, Parameters)
{
}

FDisplayClusterProjectionMeshPolicy::~FDisplayClusterProjectionMeshPolicy()
{
}

bool FDisplayClusterProjectionMeshPolicy::HandleAddViewport(const FIntPoint& InViewportSize, const uint32 InViewsAmount)
{
	check(IsInGameThread());
	check(InViewsAmount > 0);

	// policy wait for link to mesh data from BP
	// check this later, on render pass

	UE_LOG(LogDisplayClusterProjectionMesh, Log, TEXT("Mesh policy has been initialized. Wait for BP setup for viewport '%s'"), *GetViewportId());

	// Finally, initialize internal views data container
	Views.AddDefaulted(InViewsAmount);
	ViewportSize = InViewportSize;

	/* TODO: Do something with the static mesh
	UStaticMeshComponent* StaticMesh = GetStaticMeshComponent();
	ensureAlways(StaticMesh);
	*/
	return true;
}

UStaticMeshComponent* FDisplayClusterProjectionMeshPolicy::GetStaticMeshComponent() const
{
	FString ComponentId;
	// Get assigned mesh ID
	if (!DisplayClusterHelpers::map::template ExtractValue(GetParameters(), DisplayClusterProjectionStrings::cfg::mesh::Component, ComponentId))
	{
		UE_LOG(LogDisplayClusterProjectionMesh, Error, TEXT("No component ID specified for projection policy of viewport '%s'"), *GetViewportId());
		return nullptr;
	}
	
	// Get game manager interface
	const IDisplayClusterGameManager* const GameMgr = IDisplayCluster::Get().GetGameMgr();
	if (!GameMgr)
	{
		UE_LOG(LogDisplayClusterProjectionMesh, Error, TEXT("Couldn't get GameManager"));
		return nullptr;
	}

	// Get our VR root
	ADisplayClusterRootActor* const Root = GameMgr->GetRootActor();
	if (!Root)
	{
		UE_LOG(LogDisplayClusterProjectionMesh, Error, TEXT("Couldn't get a VR root object"));
		return nullptr;
	}

	// Get mesh component
	UStaticMeshComponent* MeshComponent = Root->GetMeshById(ComponentId);
	if (!MeshComponent)
	{
		UE_LOG(LogDisplayClusterProjectionMesh, Warning, TEXT("Couldn't initialize mesh component"));
		return nullptr;
	}

	return MeshComponent;
}

bool FDisplayClusterProjectionMeshPolicy::AssignWarpMesh(UStaticMeshComponent* MeshComponent, USceneComponent* OriginComponent)
{
	bool bResult = false;

	IMPCDI& MpcdiModule = IMPCDI::Get();

	FScopeLock lock(&WarpRefCS);
	if (MpcdiModule.CreateCustomRegion(DisplayClusterProjectionStrings::cfg::mesh::FileID, DisplayClusterProjectionStrings::cfg::mesh::BufferID, GetViewportId(), WarpRef))
	{
		// Always use advanced 3d profile from ext mesh as warp source
		MpcdiModule.SetMPCDIProfileType(WarpRef, IMPCDI::EMPCDIProfileType::mpcdi_A3D);
		bResult = true;
	}

	if (bResult)
	{
		bResult = MpcdiModule.SetStaticMeshWarp(WarpRef, MeshComponent, OriginComponent);
	}

	if (!bResult)
	{
		UE_LOG(LogDisplayClusterProjectionMesh, Error, TEXT("Couldn't assign warp mesh"));
		return false;
	}

	UE_LOG(LogDisplayClusterProjectionMesh, Log, TEXT("Mesh policy BP setup for viewport '%s' has been initialized."), *GetViewportId());
	return true;
}
