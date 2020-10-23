// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/DisplayClusterProjectionPolicyBase.h"

#include "DisplayClusterProjectionLog.h"

#include "IDisplayCluster.h"
#include "Game/IDisplayClusterGameManager.h"

#include "DisplayClusterRootActor.h"
#include "Components/DisplayClusterSceneComponent.h"


FDisplayClusterProjectionPolicyBase::FDisplayClusterProjectionPolicyBase(const FString& InViewportId, const TMap<FString, FString>& InParameters)
	: PolicyViewportId(InViewportId)
	, Parameters(InParameters)
{
}

FDisplayClusterProjectionPolicyBase::~FDisplayClusterProjectionPolicyBase()
{
}


void FDisplayClusterProjectionPolicyBase::InitializeOriginComponent(const FString& OriginCompId)
{
	UE_LOG(LogDisplayClusterProjection, Log, TEXT("Looking for an origin component '%s'..."), *OriginCompId);

	// Reset previous one
	PolicyOriginComponentRef.ResetSceneComponent();

	IDisplayClusterGameManager* const GameMgr = IDisplayCluster::Get().GetGameMgr();
	if (!GameMgr)
	{
		UE_LOG(LogDisplayClusterProjection, Warning, TEXT("No DisplayCluster game manager available"));
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
			UE_LOG(LogDisplayClusterProjection, Log, TEXT("No custom origin set or component '%s' not found for viewport '%s'. VR root will be used."), *OriginCompId, *PolicyViewportId);
			PolicyOriginComp = RootActor->GetRootComponent();
		}
	}

	if (!PolicyOriginComp)
	{
		UE_LOG(LogDisplayClusterProjection, Error, TEXT("Couldn't set origin component"));
		return;
	}

	PolicyOriginComponentRef.SetSceneComponent(PolicyOriginComp);
}

void FDisplayClusterProjectionPolicyBase::ReleaseOriginComponent()
{
	PolicyOriginComponentRef.ResetSceneComponent();
}

