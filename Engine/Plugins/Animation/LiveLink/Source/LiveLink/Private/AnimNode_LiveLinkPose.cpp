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
	, LiveLinkClient_AnyThread(nullptr)
	, CachedDeltaTime(0.0f)
{
}

void FAnimNode_LiveLinkPose::OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance)
{
	CurrentRetargetAsset = nullptr;

	Super::OnInitializeAnimInstance(InProxy, InAnimInstance);
}

void FAnimNode_LiveLinkPose::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	InputPose.Initialize(Context);
}

void FAnimNode_LiveLinkPose::PreUpdate(const UAnimInstance* InAnimInstance)
{
	LiveLinkClient_AnyThread = LiveLinkClient_GameThread.GetClient();

	// Protection as a class graph pin does not honor rules on abstract classes and NoClear
	UClass* RetargetAssetPtr = RetargetAsset.Get();
	if (!RetargetAssetPtr || RetargetAssetPtr->HasAnyClassFlags(CLASS_Abstract))
	{
		RetargetAssetPtr = ULiveLinkRemapAsset::StaticClass();
		RetargetAsset = RetargetAssetPtr;
	}

	if (!CurrentRetargetAsset || RetargetAssetPtr != CurrentRetargetAsset->GetClass())
	{
		CurrentRetargetAsset = NewObject<ULiveLinkRetargetAsset>(const_cast<UAnimInstance*>(InAnimInstance), RetargetAssetPtr);
		CurrentRetargetAsset->Initialize();
	}
}

void FAnimNode_LiveLinkPose::Update_AnyThread(const FAnimationUpdateContext & Context)
{
	InputPose.Update(Context);

	GetEvaluateGraphExposedInputs().Execute(Context);

	// Accumulate Delta time from update
	CachedDeltaTime += Context.GetDeltaTime();
}

void FAnimNode_LiveLinkPose::Evaluate_AnyThread(FPoseContext& Output)
{
	InputPose.Evaluate(Output);

	if (!LiveLinkClient_AnyThread || !CurrentRetargetAsset)
	{
		return;
	}

	FLiveLinkSubjectFrameData SubjectFrameData;

	TSubclassOf<ULiveLinkRole> SubjectRole = LiveLinkClient_AnyThread->GetSubjectRole(LiveLinkSubjectName);
	if (SubjectRole)
	{
		if (SubjectRole->IsChildOf(ULiveLinkAnimationRole::StaticClass()))
		{
			//Process animation data if the subject is from that type
			if (LiveLinkClient_AnyThread->EvaluateFrame_AnyThread(LiveLinkSubjectName, ULiveLinkAnimationRole::StaticClass(), SubjectFrameData))
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
			if (LiveLinkClient_AnyThread->EvaluateFrame_AnyThread(LiveLinkSubjectName, ULiveLinkBasicRole::StaticClass(), SubjectFrameData))
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

