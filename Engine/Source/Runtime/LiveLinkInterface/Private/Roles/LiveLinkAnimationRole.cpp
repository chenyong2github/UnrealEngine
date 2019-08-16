// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Roles/LiveLinkAnimationRole.h"

#include "Roles/LiveLinkAnimationBlueprintStructs.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "Roles/LiveLinkTransformRole.h"
#include "Roles/LiveLinkTransformTypes.h"

#include "LiveLinkPrivate.h"
#include "LiveLinkRoleTrait.h"
#include "LiveLinkTypes.h"

#define LOCTEXT_NAMESPACE "LiveLinkRole"

namespace LiveLinkAnimationBlendingUtil
{
	void BlendItem(const FTransform& A, const FTransform& B, FTransform& Output, float BlendWeight)
	{
		const ScalarRegister ABlendWeight(1.0f - BlendWeight);
		const ScalarRegister BBlendWeight(BlendWeight);

		Output = A * ABlendWeight;
		Output.AccumulateWithShortestRotation(B, BBlendWeight);
		Output.NormalizeRotation();
	}

	void BlendItem(const float& A, const float& B, float& Output, float BlendWeight)
	{
		Output = (A * (1.0f - BlendWeight)) + (B * BlendWeight);
	}

	template<class Type>
	void Blend(const TArray<Type>& A, const TArray<Type>& B, TArray<Type>& Output, float BlendWeight)
	{
		check(A.Num() == B.Num());
		Output.SetNum(A.Num(), false);

		for (int32 BlendIndex = 0; BlendIndex < A.Num(); ++BlendIndex)
		{
			BlendItem(A[BlendIndex], B[BlendIndex], Output[BlendIndex], BlendWeight);
		}
	}

	void CopyFrameDataBlended(const FLiveLinkAnimationFrameData& PreFrame, const FLiveLinkAnimationFrameData& PostFrame, float BlendWeight, FLiveLinkSubjectFrameData& OutFrame)
	{
		check(OutFrame.FrameData.IsValid());

		FLiveLinkAnimationFrameData* BlendedFrame = OutFrame.FrameData.Cast<FLiveLinkAnimationFrameData>();
		check(BlendedFrame);

		Blend(PreFrame.Transforms, PostFrame.Transforms, BlendedFrame->Transforms, BlendWeight);
	}

	template<class TTimeType>
	void Interpolate(TTimeType InTime, const FLiveLinkStaticDataStruct& InStaticData, const TArray<FLiveLinkFrameDataStruct>& InSourceFrames, FLiveLinkSubjectFrameData& OutBlendedFrame, bool bInInterpolatePropertyValues)
	{
		//Validate inputs
		check(InStaticData.Cast<FLiveLinkSkeletonStaticData>());

		int32 FrameDataIndexA = INDEX_NONE;
		int32 FrameDataIndexB = INDEX_NONE;
		if (ULiveLinkAnimationFrameInterpolateProcessor::FLiveLinkAnimationFrameInterpolateProcessorWorker::FindInterpolateIndex(InTime, InSourceFrames, FrameDataIndexA, FrameDataIndexB))
		{
			if (FrameDataIndexA == FrameDataIndexB)
			{
				// Copy over the frame directly
				OutBlendedFrame.FrameData.InitializeWith(InSourceFrames[FrameDataIndexA]);
			}
			else
			{
				//Initialize the output frame for animation. It will be filled during blended values copied
				OutBlendedFrame.FrameData.InitializeWith(FLiveLinkAnimationFrameData::StaticStruct(), nullptr);

				const FLiveLinkFrameDataStruct& FrameDataA = InSourceFrames[FrameDataIndexA];
				const FLiveLinkFrameDataStruct& FrameDataB = InSourceFrames[FrameDataIndexB];

				const double BlendWeight = ULiveLinkAnimationFrameInterpolateProcessor::FLiveLinkAnimationFrameInterpolateProcessorWorker::GetBlendFactor(InTime, FrameDataA, FrameDataB);
				if (FMath::IsNearlyZero(BlendWeight))
				{
					OutBlendedFrame.FrameData.InitializeWith(FrameDataA);
				}
				else if (FMath::IsNearlyEqual(1.0, BlendWeight))
				{
					OutBlendedFrame.FrameData.InitializeWith(FrameDataB);
				}
				else
				{
					const FLiveLinkAnimationFrameData* AnimationFrameDataPtrA = FrameDataA.Cast<FLiveLinkAnimationFrameData>();
					const FLiveLinkAnimationFrameData* AnimationFrameDataPtrB = FrameDataB.Cast<FLiveLinkAnimationFrameData>();
					const FLiveLinkAnimationFrameData* AnimationFrameDataPtrOutput = OutBlendedFrame.FrameData.Cast<FLiveLinkAnimationFrameData>();
					check(AnimationFrameDataPtrA && AnimationFrameDataPtrB && AnimationFrameDataPtrOutput);

					ULiveLinkAnimationFrameInterpolateProcessor::FLiveLinkAnimationFrameInterpolateProcessorWorker::FGenericInterpolateOptions InterpolationOptions;
					InterpolationOptions.bCopyClosestFrame = false; // Do not copy all the Transforms
					InterpolationOptions.bInterpolateInterpProperties = bInInterpolatePropertyValues;
					ULiveLinkAnimationFrameInterpolateProcessor::FLiveLinkAnimationFrameInterpolateProcessorWorker::GenericInterpolate(BlendWeight, InterpolationOptions, FrameDataA, FrameDataB, OutBlendedFrame.FrameData);
					LiveLinkAnimationBlendingUtil::CopyFrameDataBlended(*AnimationFrameDataPtrA, *AnimationFrameDataPtrB, BlendWeight, OutBlendedFrame);
				}
			}
		}
		else if (InSourceFrames.Num())
		{
			OutBlendedFrame.FrameData.InitializeWith(InSourceFrames[0].GetStruct(), InSourceFrames[0].GetBaseData());
		}
	}
}


/**
 * ULiveLinkAnimationRole
 */
UScriptStruct* ULiveLinkAnimationRole::GetStaticDataStruct() const
{
	return FLiveLinkSkeletonStaticData::StaticStruct();
}

UScriptStruct* ULiveLinkAnimationRole::GetFrameDataStruct() const
{
	return FLiveLinkAnimationFrameData::StaticStruct();
}

UScriptStruct* ULiveLinkAnimationRole::GetBlueprintDataStruct() const
{
	return FSubjectFrameHandle::StaticStruct();
}

bool ULiveLinkAnimationRole::InitializeBlueprintData(const FLiveLinkSubjectFrameData& InSourceData, FLiveLinkBlueprintDataStruct& OutBlueprintData) const
{
	bool bSuccess = false;

	FSubjectFrameHandle* AnimationFrameHandle = OutBlueprintData.Cast<FSubjectFrameHandle>();
	const FLiveLinkSkeletonStaticData* StaticData = InSourceData.StaticData.Cast<FLiveLinkSkeletonStaticData>();
	const FLiveLinkAnimationFrameData* FrameData = InSourceData.FrameData.Cast<FLiveLinkAnimationFrameData>();
	if (AnimationFrameHandle && StaticData && FrameData)
	{
		AnimationFrameHandle->SetCachedFrame(MakeShared<FCachedSubjectFrame>(StaticData, FrameData));
		bSuccess = true;
	}

	return bSuccess;
}

FText ULiveLinkAnimationRole::GetDisplayName() const
{
	return LOCTEXT("AnimationRole", "Animation");
}


/**
 * ULiveLinkFrameAnimationInterpolateProcessor::FLiveLinkFrameAnimationInterpolateProcessorWorker
 */
ULiveLinkAnimationFrameInterpolateProcessor::FLiveLinkAnimationFrameInterpolateProcessorWorker::FLiveLinkAnimationFrameInterpolateProcessorWorker(bool bInInterpolatePropertyValues)
	: ULiveLinkBasicFrameInterpolateProcessor::FLiveLinkBasicFrameInterpolateProcessorWorker(bInInterpolatePropertyValues)
{}

TSubclassOf<ULiveLinkRole> ULiveLinkAnimationFrameInterpolateProcessor::FLiveLinkAnimationFrameInterpolateProcessorWorker::GetRole() const
{
	return ULiveLinkAnimationRole::StaticClass();
}

void ULiveLinkAnimationFrameInterpolateProcessor::FLiveLinkAnimationFrameInterpolateProcessorWorker::Interpolate(double InTime, const FLiveLinkStaticDataStruct& InStaticData, const TArray<FLiveLinkFrameDataStruct>& InSourceFrames, FLiveLinkSubjectFrameData& OutBlendedFrame)
{
	LiveLinkAnimationBlendingUtil::Interpolate(InTime, InStaticData, InSourceFrames, OutBlendedFrame, bInterpolatePropertyValues);
}

void ULiveLinkAnimationFrameInterpolateProcessor::FLiveLinkAnimationFrameInterpolateProcessorWorker::Interpolate(const FQualifiedFrameTime& InTime, const FLiveLinkStaticDataStruct& InStaticData, const TArray<FLiveLinkFrameDataStruct>& InSourceFrames, FLiveLinkSubjectFrameData& OutBlendedFrame)
{
	LiveLinkAnimationBlendingUtil::Interpolate(InTime, InStaticData, InSourceFrames, OutBlendedFrame, bInterpolatePropertyValues);
}


/**
 * ULiveLinkFrameAnimationInterpolateProcessor
 */
TSubclassOf<ULiveLinkRole> ULiveLinkAnimationFrameInterpolateProcessor::GetRole() const
{
	return ULiveLinkAnimationRole::StaticClass();
}

ULiveLinkFrameInterpolationProcessor::FWorkerSharedPtr ULiveLinkAnimationFrameInterpolateProcessor::FetchWorker()
{
	if (!Instance.IsValid())
	{
		Instance = MakeShared<FLiveLinkAnimationFrameInterpolateProcessorWorker, ESPMode::ThreadSafe>(bInterpolatePropertyValues);
	}

	return Instance;
}


/**
 * ULiveLinkAnimationRoleToTransform::FLiveLinkAnimationRoleToTransformWorker
 */
TSubclassOf<ULiveLinkRole> ULiveLinkAnimationRoleToTransform::FLiveLinkAnimationRoleToTransformWorker::GetFromRole() const
{
	return ULiveLinkAnimationRole::StaticClass();
}

TSubclassOf<ULiveLinkRole> ULiveLinkAnimationRoleToTransform::FLiveLinkAnimationRoleToTransformWorker::GetToRole() const
{
	return ULiveLinkTransformRole::StaticClass();
}

bool ULiveLinkAnimationRoleToTransform::FLiveLinkAnimationRoleToTransformWorker::Translate(const FLiveLinkStaticDataStruct& InStaticData, const FLiveLinkFrameDataStruct& InFrameData, FLiveLinkSubjectFrameData& OutTranslatedFrame) const
{
	if (!InStaticData.IsValid() || !InFrameData.IsValid())
	{
		return false;
	}

	const FLiveLinkSkeletonStaticData* SkeletonData = InStaticData.Cast<FLiveLinkSkeletonStaticData>();
	const FLiveLinkAnimationFrameData* FrameData = InFrameData.Cast<FLiveLinkAnimationFrameData>();
	if (SkeletonData == nullptr || FrameData == nullptr)
	{
		return false;
	}

	//Allocate memory for the output translated frame with the desired type
	OutTranslatedFrame.StaticData.InitializeWith(FLiveLinkTransformStaticData::StaticStruct(), nullptr);
	OutTranslatedFrame.FrameData.InitializeWith(FLiveLinkTransformFrameData::StaticStruct(), nullptr);

	FLiveLinkTransformStaticData* TransformStaticData = OutTranslatedFrame.StaticData.Cast<FLiveLinkTransformStaticData>();
	FLiveLinkTransformFrameData* TransformFrameData = OutTranslatedFrame.FrameData.Cast<FLiveLinkTransformFrameData>();
	check(TransformStaticData && TransformFrameData);

	const int32 BoneIndex = SkeletonData->BoneNames.IndexOfByKey(BoneName);
	if (!FrameData->Transforms.IsValidIndex(BoneIndex))
	{
		return false;
	}

	//Time to translate
	TransformFrameData->MetaData = FrameData->MetaData;
	TransformFrameData->PropertyValues = FrameData->PropertyValues;
	TransformFrameData->WorldTime = FrameData->WorldTime;
	TransformFrameData->Transform = FrameData->Transforms[BoneIndex];
	return true;
}


/**
 * ULiveLinkAnimationRoleToTransform
 */
TSubclassOf<ULiveLinkRole> ULiveLinkAnimationRoleToTransform::GetFromRole() const
{
	return ULiveLinkAnimationRole::StaticClass();
}

TSubclassOf<ULiveLinkRole> ULiveLinkAnimationRoleToTransform::GetToRole() const
{
	return ULiveLinkTransformRole::StaticClass();
}

ULiveLinkFrameTranslator::FWorkerSharedPtr ULiveLinkAnimationRoleToTransform::FetchWorker()
{
	if (BoneName.IsNone())
	{
		Instance.Reset();
	}
	else if (!Instance.IsValid())
	{
		Instance = MakeShared<FLiveLinkAnimationRoleToTransformWorker, ESPMode::ThreadSafe>();
		Instance->BoneName = BoneName;
	}

	return Instance;
}

#if WITH_EDITOR
void ULiveLinkAnimationRoleToTransform::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(ThisClass, BoneName))
	{
		Instance.Reset();
	}

	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}
#endif //WITH_EDITOR

#undef LOCTEXT_NAMESPACE