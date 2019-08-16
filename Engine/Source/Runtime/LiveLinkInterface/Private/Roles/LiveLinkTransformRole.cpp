// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Roles/LiveLinkTransformRole.h"
#include "Roles/LiveLinkTransformTypes.h"

#define LOCTEXT_NAMESPACE "LiveLinkRole"

namespace
{
	EAxis::Type LiveLinkAxisToMatrixAxis(ELiveLinkAxis InAxis)
	{
		switch (InAxis)
		{
		case ELiveLinkAxis::X:
		case ELiveLinkAxis::XNeg:
			return EAxis::X;
		case ELiveLinkAxis::Y:
		case ELiveLinkAxis::YNeg:
			return EAxis::Y;
		case ELiveLinkAxis::Z:
		case ELiveLinkAxis::ZNeg:
			return EAxis::Z;
		default:
			ensureMsgf(false, TEXT("EliveLinkAxis has no default value!"));
			return EAxis::None;
		}
	}

	float AxisSign(ELiveLinkAxis InAxis)
	{
		if (InAxis == ELiveLinkAxis::X || InAxis == ELiveLinkAxis::Y || InAxis == ELiveLinkAxis::Z)
		{
			return 1.0f;
		}
		else
		{
			return -1.0f;
		}
	}
}

UScriptStruct* ULiveLinkTransformRole::GetStaticDataStruct() const
{
	return FLiveLinkTransformStaticData::StaticStruct();
}

UScriptStruct* ULiveLinkTransformRole::GetFrameDataStruct() const
{
	return FLiveLinkTransformFrameData::StaticStruct();
}

UScriptStruct* ULiveLinkTransformRole::GetBlueprintDataStruct() const
{
	return FLiveLinkTransformBlueprintData::StaticStruct();
}

bool ULiveLinkTransformRole::InitializeBlueprintData(const FLiveLinkSubjectFrameData& InSourceData, FLiveLinkBlueprintDataStruct& OutBlueprintData) const
{
	bool bSuccess = false;

	FLiveLinkTransformBlueprintData* BlueprintData = OutBlueprintData.Cast<FLiveLinkTransformBlueprintData>();
	const FLiveLinkTransformStaticData* StaticData = InSourceData.StaticData.Cast<FLiveLinkTransformStaticData>();
	const FLiveLinkTransformFrameData* FrameData = InSourceData.FrameData.Cast<FLiveLinkTransformFrameData>();
	if (BlueprintData && StaticData && FrameData)
	{
		GetStaticDataStruct()->CopyScriptStruct(&BlueprintData->StaticData, StaticData);
		GetFrameDataStruct()->CopyScriptStruct(&BlueprintData->FrameData, FrameData);
		bSuccess = true;
	}

	return bSuccess;
}

FText ULiveLinkTransformRole::GetDisplayName() const
{
	return LOCTEXT("TransformRole", "Transform");
}

/**
 * ULiveLinkAxisSwitchPreProcessor::FLiveLinkAxisSwitchPreProcessorWorker
 */
TSubclassOf<ULiveLinkRole> ULiveLinkAxisSwitchPreProcessor::FLiveLinkAxisSwitchPreProcessorWorker::GetRole() const
{
	return ULiveLinkTransformRole::StaticClass();
}

bool ULiveLinkAxisSwitchPreProcessor::FLiveLinkAxisSwitchPreProcessorWorker::PreProcessFrame(FLiveLinkFrameDataStruct& InOutFrame) const
{
	FLiveLinkTransformFrameData& TransformData = *InOutFrame.Cast<FLiveLinkTransformFrameData>();
	const FMatrix InMatrix = TransformData.Transform.ToMatrixWithScale();

	FVector DestAxisX = InMatrix.GetScaledAxis(LiveLinkAxisToMatrixAxis(AxisX)) * AxisSign(AxisX);
	FVector DestAxisY = InMatrix.GetScaledAxis(LiveLinkAxisToMatrixAxis(AxisY)) * AxisSign(AxisY);
	FVector DestAxisZ = InMatrix.GetScaledAxis(LiveLinkAxisToMatrixAxis(AxisZ)) * AxisSign(AxisZ);

	FMatrix Result(InMatrix);
	Result.SetAxes(&DestAxisX, &DestAxisY, &DestAxisZ);

	TransformData.Transform.SetFromMatrix(Result);
	return true;
}

/**
 * ULiveLinkAxisSwitchPreProcessor
 */
TSubclassOf<ULiveLinkRole> ULiveLinkAxisSwitchPreProcessor::GetRole() const
{
	return ULiveLinkTransformRole::StaticClass();
}

ULiveLinkFramePreProcessor::FWorkerSharedPtr ULiveLinkAxisSwitchPreProcessor::FetchWorker()
{
	if (!Instance.IsValid())
	{
		Instance = MakeShared<FLiveLinkAxisSwitchPreProcessorWorker, ESPMode::ThreadSafe>();
		Instance->AxisX = AxisX;
		Instance->AxisY = AxisY;
		Instance->AxisZ = AxisZ;
	}

	return Instance;
}

#if WITH_EDITOR
void ULiveLinkAxisSwitchPreProcessor::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
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

#undef LOCTEXT_NAMESPACE
