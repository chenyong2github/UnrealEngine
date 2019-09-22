// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AppleARKitPoseTrackingLiveLinkModule.h"
#include "AppleARKitSystem.h"
#include "Features/IModularFeature.h"
#include "Features/IModularFeatures.h"
#include "CoreGlobals.h"
#include "AppleARKitPoseTrackingLiveLinkImpl.h"


IMPLEMENT_MODULE(FAppleARKitPoseTrackingLiveLinkModule, AppleARKitPoseTrackingLiveLink);

DEFINE_LOG_CATEGORY(LogAppleARKitPoseTracking);

TSharedPtr<FAppleARKitPoseTrackingLiveLink, ESPMode::ThreadSafe> PoseTrackingLiveLinkInstance;

void FAppleARKitPoseTrackingLiveLinkModule::StartupModule()
{
	ensureMsgf(FModuleManager::Get().LoadModule("AppleARKit"), TEXT("ARKitPoseTrackingLiveLink depends on the AppleARKit module."));

	PoseTrackingLiveLinkInstance = MakeShared<FAppleARKitPoseTrackingLiveLink, ESPMode::ThreadSafe>();
	PoseTrackingLiveLinkInstance->Init();

	FQuat AppleForward = FVector(-1, 0, 0).ToOrientationQuat();
	FQuat MeshForward = FVector(0, 1, 0).ToOrientationQuat();;
	const auto AppleSpaceToMeshSpace = FTransform(AppleForward).Inverse() * FTransform(MeshForward);
}

void FAppleARKitPoseTrackingLiveLinkModule::ShutdownModule()
{
	PoseTrackingLiveLinkInstance->Shutdown();
	PoseTrackingLiveLinkInstance = nullptr;
}

