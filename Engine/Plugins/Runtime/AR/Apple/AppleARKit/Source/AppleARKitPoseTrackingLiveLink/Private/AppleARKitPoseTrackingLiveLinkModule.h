// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Stats/Stats.h"
#include "Modules/ModuleManager.h"

class APPLEARKITPOSETRACKINGLIVELINK_API FAppleARKitPoseTrackingLiveLinkModule :
	public IModuleInterface
{
public:
	virtual void StartupModule() override;

	virtual void ShutdownModule() override;
};

DECLARE_LOG_CATEGORY_EXTERN(LogAppleARKitPoseTracking, Log, All);

DECLARE_STATS_GROUP(TEXT("PoseTracking AR"), STATGROUP_PoseTrackingAR, STATCAT_Advanced);
