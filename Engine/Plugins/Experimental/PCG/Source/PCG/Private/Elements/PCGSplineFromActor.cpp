// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGSplineFromActor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGSplineFromActor)

#define LOCTEXT_NAMESPACE "PCGSplineFromActorElement"

UPCGSplineFromActorSettings::UPCGSplineFromActorSettings()
{
	bDisplayModeSettings = false;
	Mode = EPCGGetDataFromActorMode::ParseActorComponents;
}

TArray<FPCGPinProperties> UPCGSplineFromActorSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Spline);

	return PinProperties;
}

#if WITH_EDITOR
FText UPCGSplineFromActorSettings::GetNodeTooltipText() const
{
	return LOCTEXT("SplineFromActorTooltip", "Builds a collection of splines from the selected actors.");
}
#endif

FPCGElementPtr UPCGSplineFromActorSettings::CreateElement() const
{
	return MakeShared<FPCGDataFromActorElement>();
}

#undef LOCTEXT_NAMESPACE
