// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/PicpProjectionPolicyBase.h"

#include "PicpProjectionLog.h"

#include "IDisplayCluster.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "Game/IDisplayClusterGameManager.h"

#include "DisplayClusterRootActor.h"
#include "Components/DisplayClusterSceneComponent.h"


FPicpProjectionPolicyBase::FPicpProjectionPolicyBase(const FString& InViewportId, const TMap<FString, FString>& InParameters)
	: PolicyViewportId(InViewportId)
	, Parameters(InParameters)
{
}

FPicpProjectionPolicyBase::~FPicpProjectionPolicyBase()
{
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
