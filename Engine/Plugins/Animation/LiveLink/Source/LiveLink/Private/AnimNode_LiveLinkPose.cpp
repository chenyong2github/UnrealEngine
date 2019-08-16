// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimNode_LiveLinkPose.h"
#include "ILiveLinkClient.h"

#include "Features/IModularFeatures.h"

#include "Animation/AnimInstanceProxy.h"
#include "LiveLinkCustomVersion.h"
#include "LiveLinkRemapAsset.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"

FAnimNode_LiveLinkPose::FAnimNode_LiveLinkPose() 
	: RetargetAsset(ULiveLinkRemapAsset::StaticClass())
	, CurrentRetargetAsset(nullptr)
	, LiveLinkClient(nullptr)
	, CachedDeltaTime(0.0f)
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

	FLiveLinkSubjectFrameData SubjectFrameData;

	TSubclassOf<ULiveLinkRole> SubjectRole = LiveLinkClient->GetSubjectRole(LiveLinkSubjectName);
	if (SubjectRole->IsChildOf(ULiveLinkAnimationRole::StaticClass()))
	{
		//Process animation data if the subject is from that type
		if (LiveLinkClient->EvaluateFrame_AnyThread(LiveLinkSubjectName, ULiveLinkAnimationRole::StaticClass(), SubjectFrameData))
		{
			FLiveLinkSkeletonStaticData* SkeletonData = SubjectFrameData.StaticData.Cast<FLiveLinkSkeletonStaticData>();
			FLiveLinkAnimationFrameData* FrameData = SubjectFrameData.FrameData.Cast<FLiveLinkAnimationFrameData>();
			check(SkeletonData);
			check(FrameData);

			CurrentRetargetAsset->BuildPoseFromAnimationData(CachedDeltaTime, SkeletonData, FrameData, Output.Pose);
			CurrentRetargetAsset->BuildPoseAndCurveFromBaseData(CachedDeltaTime, SkeletonData, FrameData, Output.Pose, Output.Curve);
			CachedDeltaTime = 0.f; // Reset so that if we evaluate again we don't "create" time inside of the retargeter
		}
	}
	else
	{
		//Otherwise, fetch basic data that contains property / curve data
		if (LiveLinkClient->EvaluateFrame_AnyThread(LiveLinkSubjectName, ULiveLinkBasicRole::StaticClass(), SubjectFrameData))
		{
			FLiveLinkBaseStaticData* BaseStaticData = SubjectFrameData.StaticData.Cast<FLiveLinkBaseStaticData>();
			FLiveLinkBaseFrameData* BaseFrameData = SubjectFrameData.FrameData.Cast<FLiveLinkBaseFrameData>();
			check(BaseStaticData);
			check(BaseFrameData);

			CurrentRetargetAsset->BuildPoseAndCurveFromBaseData(CachedDeltaTime, BaseStaticData, BaseFrameData, Output.Pose, Output.Curve);
			CachedDeltaTime = 0.f; // Reset so that if we evaluate again we don't "create" time inside of the retargeter
		}
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
	FString DebugLine = FString::Printf(TEXT("LiveLink - SubjectName: %s"), *LiveLinkSubjectName.ToString());

	DebugData.AddDebugItem(DebugLine);
	InputPose.GatherDebugData(DebugData);
}

bool FAnimNode_LiveLinkPose::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FLiveLinkCustomVersion::GUID);
	
	UScriptStruct* Struct = FAnimNode_LiveLinkPose::StaticStruct();
	
	if (Ar.IsLoading() || Ar.IsSaving())
	{
		Struct->SerializeTaggedProperties(Ar, (uint8*)this, Struct, nullptr);
	}

#if WITH_EDITORONLY_DATA
	//Take old data and put it in new data structure
	if (Ar.IsLoading())
	{
		if (Ar.CustomVer(FLiveLinkCustomVersion::GUID) < FLiveLinkCustomVersion::NewLiveLinkRoleSystem)
		{
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			LiveLinkSubjectName.Name = SubjectName_DEPRECATED;
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}
	}
#endif

	return true;
}

