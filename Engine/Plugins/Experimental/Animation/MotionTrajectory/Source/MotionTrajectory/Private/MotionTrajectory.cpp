// Copyright Epic Games, Inc. All Rights Reserved.

#include "MotionTrajectory.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "MotionTrajectory"

DEFINE_LOG_CATEGORY(LogMotionTrajectory);

void FMotionTrajectoryModule::StartupModule()
{
}

void FMotionTrajectoryModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FMotionTrajectoryModule, MotionTrajectory)
