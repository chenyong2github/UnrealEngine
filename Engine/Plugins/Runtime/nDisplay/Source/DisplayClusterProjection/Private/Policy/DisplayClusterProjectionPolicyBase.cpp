// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Policy/DisplayClusterProjectionPolicyBase.h"

#include "DisplayClusterProjectionLog.h"

#include "IDisplayCluster.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "Game/IDisplayClusterGameManager.h"

#include "DisplayClusterSceneComponent.h"
#include "DisplayClusterRootComponent.h"


FDisplayClusterProjectionPolicyBase::FDisplayClusterProjectionPolicyBase(const FString& InViewportId)
	: PolicyViewportId(InViewportId)
{
}

FDisplayClusterProjectionPolicyBase::~FDisplayClusterProjectionPolicyBase()
{
}


void FDisplayClusterProjectionPolicyBase::InitializeOriginComponent(const FString& OriginCompId)
{
	UE_LOG(LogDisplayClusterProjection, Log, TEXT("Looking for an origin component '%s'..."), *OriginCompId);

	IDisplayClusterGameManager* const GameMgr = IDisplayCluster::Get().GetGameMgr();
	if (!GameMgr)
	{
		UE_LOG(LogDisplayClusterProjection, Warning, TEXT("No DisplayCluster game manager available"));
		return;
	}

	if (!OriginCompId.IsEmpty())
	{
		// Try to get a node specified in the config file
		PolicyOriginComp = GameMgr->GetNodeById(OriginCompId);
	}

	if(PolicyOriginComp == nullptr)
	{
		UE_LOG(LogDisplayClusterProjection, Log, TEXT("No custom origin set or component '%s' not found for viewport '%s'. VR root will be used."), *OriginCompId, *PolicyViewportId);
		PolicyOriginComp = GameMgr->GetRootComponent();
	}

	if (!PolicyOriginComp)
	{
		UE_LOG(LogDisplayClusterProjection, Error, TEXT("Couldn't set origin component"));
		return;
	}
}
