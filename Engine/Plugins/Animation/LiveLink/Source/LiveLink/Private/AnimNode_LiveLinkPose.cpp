// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimNode_LiveLinkPose.h"
#include "ILiveLinkClient.h"

#include "Features/IModularFeatures.h"

#include "Animation/AnimInstanceProxy.h"
#include "LiveLinkRemapAsset.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"

FAnimNode_LiveLinkPose::FAnimNode_LiveLinkPose() 
	: RetargetAsset(ULiveLinkRemapAsset::StaticClass())
	, CurrentRetargetAsset(nullptr)
	, LiveLinkClient(nullptr)
{
}

void FAnimNode_LiveLinkPose::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();

	if (ModularFeatures.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		LiveLinkClient = &IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
	}

	CurrentRetargetAsset = nullptr;

	InputPose.Initialize(Context);
}

void FAnimNode_LiveLinkPose::Update_AnyThread(const FAnimationUpdateContext & Context)
{
	InputPose.Update(Context);

	GetEvaluateGraphExposedInputs().Execute(Context);

	// Accumulate Delta time from update
	CachedDeltaTime += Context.GetDeltaTime();

	// Protection as a class graph pin does not honour rules on abstract classes and NoClear
	if (!RetargetAsset.Get() || RetargetAsset.Get()->HasAnyClassFlags(CLASS_Abstract))
	{
		RetargetAsset = ULiveLinkRemapAsset::StaticClass();
	}

	if (!CurrentRetargetAsset || RetargetAsset != CurrentRetargetAsset->GetClass())
	{
		CurrentRetargetAsset = NewObject<ULiveLinkRetargetAsset>(Context.AnimInstanceProxy->GetAnimInstanceObject(), *RetargetAsset);
		CurrentRetargetAsset->Initialize();
	}
}

void FAnimNode_LiveLinkPose::Evaluate_AnyThread(FPoseContext& Output)
{
	InputPose.Evaluate(Output);

	if (!LiveLinkClient || !CurrentRetargetAsset)
	{
		return;
	}

	FLiveLinkSubjectFrameData AnimationFrame;
	if(LiveLinkClient->EvaluateFrame_AnyThread(SubjectName, ULiveLinkAnimationRole::StaticClass(), AnimationFrame))
	{
		FLiveLinkSkeletonStaticData* SkeletonData = AnimationFrame.StaticData.Cast<FLiveLinkSkeletonStaticData>();
		FLiveLinkAnimationFrameData* FrameData = AnimationFrame.FrameData.Cast<FLiveLinkAnimationFrameData>();
		check(SkeletonData);
		check(FrameData);

		CurrentRetargetAsset->BuildPoseForSubject(CachedDeltaTime, SkeletonData, FrameData, Output.Pose, Output.Curve);
		CachedDeltaTime = 0.f; // Reset so that if we evaluate again we don't "create" time inside of the retargeter
	}
}


void FAnimNode_LiveLinkPose::OnLiveLinkClientRegistered(const FName& Type, class IModularFeature* ModularFeature)
{
	if (Type == ILiveLinkClient::ModularFeatureName && !LiveLinkClient)
	{
		LiveLinkClient = static_cast<ILiveLinkClient*>(ModularFeature);
	}
}

void FAnimNode_LiveLinkPose::OnLiveLinkClientUnregistered(const FName& Type, class IModularFeature* ModularFeature)
{
	if (Type == ILiveLinkClient::ModularFeatureName && ModularFeature == LiveLinkClient)
	{
		LiveLinkClient = nullptr;
	}
}

void FAnimNode_LiveLinkPose::CacheBones_AnyThread(const FAnimationCacheBonesContext & Context)
{
	Super::CacheBones_AnyThread(Context);
	InputPose.CacheBones(Context);
}

void FAnimNode_LiveLinkPose::GatherDebugData(FNodeDebugData& DebugData)
{
	FString DebugLine = FString::Printf(TEXT("LiveLink - SubjectName: %s"), *SubjectName.ToString());

	DebugData.AddDebugItem(DebugLine);
	InputPose.GatherDebugData(DebugData);
}