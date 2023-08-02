// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGBranch.h"

#include "PCGCommon.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "Elements/PCGGather.h"

#define LOCTEXT_NAMESPACE "FPCGBranchElement"

namespace PCGBranchConstants
{
	const FName InputLabelA = TEXT("Input A");
	const FName InputLabelB = TEXT("Input B");
	const FText NodeTitleBase = LOCTEXT("NodeTitle", "Branch");
}

#if WITH_EDITOR
FText UPCGBranchSettings::GetDefaultNodeTitle() const
{
	// TODO: This should statically update or dynamically update, if overridden, for which branch was taken, ie. Branch (A)
	return PCGBranchConstants::NodeTitleBase;
}

FText UPCGBranchSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Control flow node that will allow all input data on either Pin A or Pin B only, based on the 'Use Input B' property - which can also be overridden.");
}
#endif // WITH_EDITOR

EPCGDataType UPCGBranchSettings::GetCurrentPinTypes(const UPCGPin* InPin) const
{
	check(InPin);
	if (!InPin->IsOutputPin())
	{
		return Super::GetCurrentPinTypes(InPin);
	}

	// Output pin narrows to union of inputs on both
	const EPCGDataType InputTypeUnion = GetTypeUnionOfIncidentEdges(PCGBranchConstants::InputLabelA) | GetTypeUnionOfIncidentEdges(PCGBranchConstants::InputLabelB);
	return InputTypeUnion != EPCGDataType::None ? InputTypeUnion : EPCGDataType::Any;
}


TArray<FPCGPinProperties> UPCGBranchSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGBranchConstants::InputLabelA,
		EPCGDataType::Any,
		/*bInAllowMultipleConnections=*/true,
		/*bAllowMultipleData=*/true,
		LOCTEXT("FirstInputPinTooltip", "Will only be used if 'Use Input B' (overridable) is false"));
	PinProperties.Emplace(PCGBranchConstants::InputLabelB,
		EPCGDataType::Any,
		/*bInAllowMultipleConnections=*/true,
		/*bAllowMultipleData=*/true,
		LOCTEXT("SecondInputPinTooltip", "Will only be used if 'Use Input B' (overridable) is true"));

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGBranchSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Any, /*bInAllowMultipleConnections=*/true, /*bAllowMultipleData=*/true, LOCTEXT("OutputPinTooltip", "All input will gathered into a single data collection"));

	return PinProperties;
}

FPCGElementPtr UPCGBranchSettings::CreateElement() const
{
	return MakeShared<FPCGBranchElement>();
}

bool FPCGBranchElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGBranchElement::ExecuteInternal);

	const UPCGBranchSettings* Settings = Context->GetInputSettings<UPCGBranchSettings>();
	check(Settings);

	const FName SelectedPinLabel = Settings->bUseInputB ? PCGBranchConstants::InputLabelB : PCGBranchConstants::InputLabelA;

	// Reuse the functionality of the Gather Node
	Context->OutputData = PCGGather::GatherDataForPin(Context->InputData, SelectedPinLabel);

	return true;
}

#undef LOCTEXT_NAMESPACE