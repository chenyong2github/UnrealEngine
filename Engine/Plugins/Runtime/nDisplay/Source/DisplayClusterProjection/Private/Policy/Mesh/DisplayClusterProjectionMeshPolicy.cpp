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
#include "ProceduralMeshComponent.h"

FDisplayClusterProjectionMeshPolicy::FDisplayClusterProjectionMeshPolicy(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy)
	: FDisplayClusterProjectionMPCDIPolicy(ProjectionPolicyId, InConfigurationProjectionPolicy)
{
}

bool FDisplayClusterProjectionMeshPolicy::CreateWarpMeshInterface(class IDisplayClusterViewport* InViewport)
{
	check(IsInGameThread());

	if (WarpBlendInterface.IsValid() == false)
	{
		FWarpMeshConfiguration WarpCfg;
		if (!GetWarpMeshConfiguration(InViewport, WarpCfg))
		{
			return false;
		}

		IDisplayClusterShaders& ShadersAPI = IDisplayClusterShaders::Get();
		bool bResult = false;

		if (WarpCfg.StaticMeshComponent != nullptr)
		{
			FDisplayClusterWarpBlendConstruct::FAssignWarpMesh CreateParameters;
			CreateParameters.MeshComponent   = WarpCfg.StaticMeshComponent;
			CreateParameters.OriginComponent = WarpCfg.OriginComponent;

			bResult = ShadersAPI.GetWarpBlendManager().Create(CreateParameters, WarpBlendInterface);
		}
		else
		{
			FDisplayClusterWarpBlendConstruct::FAssignWarpProceduralMesh CreateParameters;
			CreateParameters.OriginComponent = WarpCfg.OriginComponent;

			CreateParameters.ProceduralMeshComponent = WarpCfg.ProceduralMeshComponent;
			CreateParameters.ProceduralMeshComponentSectionIndex = WarpCfg.SectionIndex;

			bResult = ShadersAPI.GetWarpBlendManager().Create(CreateParameters, WarpBlendInterface);
		}

		if (!bResult)
		{
			if (!IsEditorOperationMode())
			{
				UE_LOG(LogDisplayClusterProjectionMesh, Warning, TEXT("Couldn't create mesh warpblend interface"));
			}

			return false;
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
		if (!IsEditorOperationMode())
		{
			UE_LOG(LogDisplayClusterProjectionMesh, Error, TEXT("Couldn't create warp interface for viewport '%s'"), *InViewport->GetId());
		}

		return false;
	}

	// Finally, initialize internal views data container
	WarpBlendContexts.AddDefaulted(2);

	return true;
}

bool FDisplayClusterProjectionMeshPolicy::GetWarpMeshConfiguration(IDisplayClusterViewport* InViewport, FWarpMeshConfiguration& OutWarpCfg)
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

		if (!IsEditorOperationMode())
		{
			UE_LOG(LogDisplayClusterProjectionMesh, Error, TEXT("No component ID '%s' specified for projection policy '%s'"), *ComponentId, *GetId());
		}

		return false;
	}

	// Get the StaticMeshComponent
	OutWarpCfg.StaticMeshComponent = Root->GetComponentByName<UStaticMeshComponent>(ComponentId);
	if (OutWarpCfg.StaticMeshComponent == nullptr)
	{
		// Get the ProceduralMeshComponent
		OutWarpCfg.ProceduralMeshComponent = Root->GetComponentByName<UProceduralMeshComponent>(ComponentId);
		if (OutWarpCfg.ProceduralMeshComponent == nullptr)
		{
			if (!IsEditorOperationMode())
			{
				UE_LOG(LogDisplayClusterProjectionMesh, Warning, TEXT("Couldn't initialize mesh component '%s'"), *ComponentId);
			}

			return false;
		}

		int CfgSectionIndex;
		if (DisplayClusterHelpers::map::template ExtractValueFromString(GetParameters(), DisplayClusterProjectionStrings::cfg::mesh::SectionIndex, CfgSectionIndex))
		{
			if (CfgSectionIndex >= 0)
			{
				UE_LOG(LogDisplayClusterProjectionMesh, Verbose, TEXT("Found SectionIndex value - '%d'"), CfgSectionIndex);

				OutWarpCfg.SectionIndex = CfgSectionIndex;
			}
			else
			{
				UE_LOG(LogDisplayClusterProjectionMesh, Error, TEXT("Invalid SectionIndex value - '%d'"), CfgSectionIndex);
			}
		}
	}

	OutWarpCfg.OriginComponent = Root->GetRootComponent();

	return true;
}

#if WITH_EDITOR
//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicyPreview
//////////////////////////////////////////////////////////////////////////////////////////////
UMeshComponent* FDisplayClusterProjectionMeshPolicy::GetOrCreatePreviewMeshComponent(IDisplayClusterViewport* InViewport, bool& bOutIsRootActorComponent)
{
	bOutIsRootActorComponent = true;

	FWarpMeshConfiguration WarpCfg;
	if (GetWarpMeshConfiguration(InViewport, WarpCfg))
	{
		if(WarpCfg.StaticMeshComponent != nullptr)
		{
			return WarpCfg.StaticMeshComponent;
		}

		if (WarpCfg.ProceduralMeshComponent != nullptr)
		{
			return WarpCfg.ProceduralMeshComponent;
		}
	}

	return nullptr;
}
#endif

