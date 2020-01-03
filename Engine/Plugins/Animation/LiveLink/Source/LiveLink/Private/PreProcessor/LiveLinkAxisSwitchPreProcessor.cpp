// Copyright Epic Games, Inc. All Rights Reserved.

#include "PreProcessor/LiveLinkAxisSwitchPreProcessor.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "Roles/LiveLinkTransformRole.h"
#include "Roles/LiveLinkTransformTypes.h"

namespace
{
	EAxis::Type LiveLinkAxisToMatrixAxis(ELiveLinkAxis InAxis)
	{
		const uint8 Value = static_cast<uint8>(InAxis) % 3;
		const EAxis::Type Results[] = {EAxis::X, EAxis::Y, EAxis::Z};

		return Results[Value];
	}

	float AxisSign(ELiveLinkAxis InAxis)
	{
		return static_cast<uint8>(InAxis) < 3 ? 1.f : -1.f;
	}

	void SwitchTransform(FTransform& Transform, ELiveLinkAxis AxisX, ELiveLinkAxis AxisY, ELiveLinkAxis AxisZ)
	{
		const FMatrix InMatrix = Transform.ToMatrixWithScale();

		FVector DestAxisX = InMatrix.GetScaledAxis(LiveLinkAxisToMatrixAxis(AxisX)) * AxisSign(AxisX);
		FVector DestAxisY = InMatrix.GetScaledAxis(LiveLinkAxisToMatrixAxis(AxisY)) * AxisSign(AxisY);
		FVector DestAxisZ = InMatrix.GetScaledAxis(LiveLinkAxisToMatrixAxis(AxisZ)) * AxisSign(AxisZ);

		FMatrix Result(InMatrix);
		Result.SetAxes(&DestAxisX, &DestAxisY, &DestAxisZ);

		Transform.SetFromMatrix(Result);
	}
}

/**
 * ULiveLinkTransformAxisSwitchPreProcessor::FLiveLinkTransformAxisSwitchPreProcessorWorker
 */
TSubclassOf<ULiveLinkRole> ULiveLinkTransformAxisSwitchPreProcessor::FLiveLinkTransformAxisSwitchPreProcessorWorker::GetRole() const
{
	return ULiveLinkTransformRole::StaticClass();
}

bool ULiveLinkTransformAxisSwitchPreProcessor::FLiveLinkTransformAxisSwitchPreProcessorWorker::PreProcessFrame(FLiveLinkFrameDataStruct& InOutFrame) const
{
	FLiveLinkTransformFrameData& TransformData = *InOutFrame.Cast<FLiveLinkTransformFrameData>();
	SwitchTransform(TransformData.Transform, AxisX, AxisY, AxisZ);
	return true;
}

/**
 * ULiveLinkTransformAxisSwitchPreProcessor
 */
TSubclassOf<ULiveLinkRole> ULiveLinkTransformAxisSwitchPreProcessor::GetRole() const
{
	return ULiveLinkTransformRole::StaticClass();
}

ULiveLinkFramePreProcessor::FWorkerSharedPtr ULiveLinkTransformAxisSwitchPreProcessor::FetchWorker()
{
	if (!Instance.IsValid())
	{
		Instance = MakeShared<FLiveLinkTransformAxisSwitchPreProcessorWorker, ESPMode::ThreadSafe>();
		Instance->AxisX = AxisX;
		Instance->AxisY = AxisY;
		Instance->AxisZ = AxisZ;
	}

	return Instance;
}

#if WITH_EDITOR
void ULiveLinkTransformAxisSwitchPreProcessor::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(ThisClass, AxisX) ||
		PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(ThisClass, AxisY) ||
		PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(ThisClass, AxisZ))
	{
		Instance.Reset();
	}

	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}
#endif //WITH_EDITOR


/**
 * ULiveLinkAnimationAxisSwitchPreProcessor::FLiveLinkAnimationAxisSwitchPreProcessorWorker
 */
TSubclassOf<ULiveLinkRole> ULiveLinkAnimationAxisSwitchPreProcessor::FLiveLinkAnimationAxisSwitchPreProcessorWorker::GetRole() const
{
	return ULiveLinkAnimationRole::StaticClass();
}

bool ULiveLinkAnimationAxisSwitchPreProcessor::FLiveLinkAnimationAxisSwitchPreProcessorWorker::PreProcessFrame(FLiveLinkFrameDataStruct& InOutFrame) const
{
	FLiveLinkAnimationFrameData& AnimationData = *InOutFrame.Cast<FLiveLinkAnimationFrameData>();
	
	for (FTransform& Transform : AnimationData.Transforms)
	{
		SwitchTransform(Transform, AxisX, AxisY, AxisZ);
	}
	
	return true;
}

/**
 * ULiveLinkAnimationAxisSwitchPreProcessor
 */
TSubclassOf<ULiveLinkRole> ULiveLinkAnimationAxisSwitchPreProcessor::GetRole() const
{
	return ULiveLinkAnimationRole::StaticClass();
}

ULiveLinkFramePreProcessor::FWorkerSharedPtr ULiveLinkAnimationAxisSwitchPreProcessor::FetchWorker()
{
	if (!Instance.IsValid())
	{
		Instance = MakeShared<FLiveLinkAnimationAxisSwitchPreProcessorWorker, ESPMode::ThreadSafe>();
		Instance->AxisX = AxisX;
		Instance->AxisY = AxisY;
		Instance->AxisZ = AxisZ;
	}

	return Instance;
}