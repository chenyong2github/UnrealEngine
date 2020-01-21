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

	float LiveLinkAxisToVectorMember(ELiveLinkAxis InAxis, const FVector& Origin)
	{
		const uint8 Value = static_cast<uint8>(InAxis) % 3;
		return Origin[Value];
	}

	float AxisSign(ELiveLinkAxis InAxis)
	{
		return static_cast<uint8>(InAxis) < 3 ? 1.f : -1.f;
	}

	void SwitchTransform(FTransform& Transform
		, ELiveLinkAxis OrientationAxisX, ELiveLinkAxis OrientationAxisY, ELiveLinkAxis OrientationAxisZ
		, ELiveLinkAxis TranslationAxisX, ELiveLinkAxis TranslationAxisY, ELiveLinkAxis TranslationAxisZ)
	{
		const FMatrix InMatrix = Transform.ToMatrixWithScale();

		FVector DestAxisX = InMatrix.GetScaledAxis(LiveLinkAxisToMatrixAxis(OrientationAxisX)) * AxisSign(OrientationAxisX);
		FVector DestAxisY = InMatrix.GetScaledAxis(LiveLinkAxisToMatrixAxis(OrientationAxisY)) * AxisSign(OrientationAxisY);
		FVector DestAxisZ = InMatrix.GetScaledAxis(LiveLinkAxisToMatrixAxis(OrientationAxisZ)) * AxisSign(OrientationAxisZ);

		FVector Origin = InMatrix.GetOrigin();
		FVector NewOrigin;
		NewOrigin.X = LiveLinkAxisToVectorMember(TranslationAxisX, Origin) * AxisSign(TranslationAxisX);
		NewOrigin.Y = LiveLinkAxisToVectorMember(TranslationAxisY, Origin) * AxisSign(TranslationAxisY);
		NewOrigin.Z = LiveLinkAxisToVectorMember(TranslationAxisZ, Origin) * AxisSign(TranslationAxisZ);

		FMatrix Result(InMatrix);
		Result.SetAxes(&DestAxisX, &DestAxisY, &DestAxisZ, &NewOrigin);

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
	SwitchTransform(TransformData.Transform, OrientationAxisX, OrientationAxisY, OrientationAxisZ, TranslationAxisX, TranslationAxisY, TranslationAxisZ);
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
		Instance->OrientationAxisX = OrientationAxisX;
		Instance->OrientationAxisY = OrientationAxisY;
		Instance->OrientationAxisZ = OrientationAxisZ;
		Instance->TranslationAxisX = TranslationAxisX;
		Instance->TranslationAxisY = TranslationAxisY;
		Instance->TranslationAxisZ = TranslationAxisZ;
	}

	return Instance;
}

#if WITH_EDITOR
void ULiveLinkTransformAxisSwitchPreProcessor::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(ThisClass, OrientationAxisX) ||
		PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(ThisClass, OrientationAxisY) ||
		PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(ThisClass, OrientationAxisZ) ||
		PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(ThisClass, TranslationAxisX) ||
		PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(ThisClass, TranslationAxisY) ||
		PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(ThisClass, TranslationAxisZ))
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
		SwitchTransform(Transform, OrientationAxisX, OrientationAxisY, OrientationAxisZ, TranslationAxisX, TranslationAxisY, TranslationAxisZ);
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
		Instance->OrientationAxisX = OrientationAxisX;
		Instance->OrientationAxisY = OrientationAxisY;
		Instance->OrientationAxisZ = OrientationAxisZ;
		Instance->TranslationAxisX = TranslationAxisX;
		Instance->TranslationAxisY = TranslationAxisY;
		Instance->TranslationAxisZ = TranslationAxisZ;
	}

	return Instance;
}
