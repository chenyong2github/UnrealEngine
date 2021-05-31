// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/Mesh/DisplayClusterProjectionMeshPolicy.h"
#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"
#include "DisplayClusterRootActor.h"

#include "Misc/DisplayClusterHelpers.h"

#include "Render/Viewport/IDisplayClusterViewportManager.h"
#include "Render/Viewport/IDisplayClusterViewport.h"

#include "IDisplayClusterShaders.h"
#include "WarpBlend/IDisplayClusterWarpBlend.h"
#include "WarpBlend/IDisplayClusterWarpBlendManager.h"

#include "Components/StaticMeshComponent.h"


FDisplayClusterProjectionMeshPolicy::FDisplayClusterProjectionMeshPolicy(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy)
	: FDisplayClusterProjectionMPCDIPolicy(ProjectionPolicyId, InConfigurationProjectionPolicy)
{
}

FDisplayClusterProjectionMeshPolicy::~FDisplayClusterProjectionMeshPolicy()
{
	WarpBlendInterface.Reset();
	WarpBlendInterface_Proxy.Reset();
}

bool FDisplayClusterProjectionMeshPolicy::CreateWarpMeshInterface(class IDisplayClusterViewport* InViewport)
{
	check(IsInGameThread());

	if (WarpBlendInterface == nullptr)
	{
		FDisplayClusterWarpBlendConstruct::FAssignWarpMesh CreateParameters;
		if (GetWarpMeshAndOrigin(InViewport, CreateParameters.MeshComponent, CreateParameters.OriginComponent))
		{
			IDisplayClusterShaders& ShadersAPI = IDisplayClusterShaders::Get();

			if (!ShadersAPI.GetWarpBlendManager().Create(CreateParameters, WarpBlendInterface))
			{
				UE_LOG(LogDisplayClusterProjectionMesh, Warning, TEXT("Couldn't create mesh warpblend interface"));
				return false;
			}
		}
	}

	return true;
}

bool FDisplayClusterProjectionMeshPolicy::HandleStartScene(class IDisplayClusterViewport* InViewport)
{
	check(IsInGameThread());

	// The game side of the nDisplay has been initialized by the nDisplay Game Manager already
	// so we can extend it by our projection related functionality/components/etc.

	WarpBlendContexts.Empty();

	// Find origin component if it exists
	InitializeOriginComponent(InViewport, OriginCompId);

	if (!CreateWarpMeshInterface(InViewport))
	{
		UE_LOG(LogDisplayClusterProjectionMesh, Error, TEXT("Couldn't create warp interface for viewport '%s'"), *InViewport->GetId());

		return false;
	}

	// Finally, initialize internal views data container
	WarpBlendContexts.AddDefaulted(2);

	return true;
}

bool FDisplayClusterProjectionMeshPolicy::GetWarpMeshAndOrigin(class IDisplayClusterViewport* InViewport, class UStaticMeshComponent*& OutMeshComponent, class USceneComponent*& OutOriginComponent)
{
	check(InViewport);

	// Get our VR root
	ADisplayClusterRootActor* const Root = InViewport->GetOwner().GetRootActor();
	if (!Root)
	{
		UE_LOG(LogDisplayClusterProjectionMesh, Error, TEXT("Couldn't get a VR root object"));
		return false;
	}

	FString ComponentId;
	// Get assigned mesh ID
	if (!DisplayClusterHelpers::map::template ExtractValue(GetParameters(), DisplayClusterProjectionStrings::cfg::mesh::Component, ComponentId))
	{

#if WITH_EDITOR
		if (ComponentId.IsEmpty())
		{
			return false;
		}
#endif

		UE_LOG(LogDisplayClusterProjectionMesh, Error, TEXT("No component ID '%s' specified for projection policy '%s'"), *ComponentId, *GetId());
		return false;
	}

	// Get mesh component
	UStaticMeshComponent* MeshComponent = Root->GetMeshById(ComponentId);
	if (!MeshComponent)
	{
		UE_LOG(LogDisplayClusterProjectionMesh, Warning, TEXT("Couldn't initialize mesh component '%s'"), *ComponentId);
		return false;
	}

	OutMeshComponent = MeshComponent;
	OutOriginComponent = Root->GetRootComponent();

	return true;
}

#if WITH_EDITOR
//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicyPreview
//////////////////////////////////////////////////////////////////////////////////////////////
UMeshComponent* FDisplayClusterProjectionMeshPolicy::GetOrCreatePreviewMeshComponent(IDisplayClusterViewport* InViewport)
{
	UStaticMeshComponent* MeshComponent = nullptr;
	USceneComponent* OriginComponent = nullptr;

	GetWarpMeshAndOrigin(InViewport, MeshComponent, OriginComponent);

	return MeshComponent;
}
#endif

