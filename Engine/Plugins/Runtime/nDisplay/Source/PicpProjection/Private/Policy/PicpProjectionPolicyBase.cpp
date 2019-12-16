// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Policy/PicpProjectionPolicyBase.h"

#include "PicpProjectionLog.h"

#include "IDisplayCluster.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "Game/IDisplayClusterGameManager.h"

#include "DisplayClusterRootComponent.h"
#include "DisplayClusterSceneComponent.h"


FPicpProjectionPolicyBase::FPicpProjectionPolicyBase(const FString& InViewportId)
	: PolicyViewportId(InViewportId)
{
}

FPicpProjectionPolicyBase::~FPicpProjectionPolicyBase()
{
}


void FPicpProjectionPolicyBase::InitializeOriginComponent(const FString& OriginCompId)
{
	UE_LOG(LogPicpProjectionMPCDI, Log, TEXT("Looking for an origin component '%s'..."), *OriginCompId);

	IDisplayClusterGameManager* const GameMgr = IDisplayCluster::Get().GetGameMgr();
	if (!GameMgr)
	{
		UE_LOG(LogPicpProjectionMPCDI, Warning, TEXT("No DisplayCluster game manager available"));
		return;
	}

	if (!OriginCompId.IsEmpty())
	{
		// Try to get a node specified in the config file
		PolicyOriginComp = GameMgr->GetNodeById(OriginCompId);
	}

	if(PolicyOriginComp == nullptr)
	{
		UE_LOG(LogPicpProjectionMPCDI, Log, TEXT("No custom origin set or component '%s' not found for viewport '%s'. VR root will be used."), *OriginCompId, *PolicyViewportId);
		PolicyOriginComp = GameMgr->GetRootComponent();
	}

	if (!PolicyOriginComp)
	{
		UE_LOG(LogPicpProjectionMPCDI, Error, TEXT("Couldn't set origin component"));
		return;
	}
}
