// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGInputOutputSettings.h"

bool FPCGInputOutputElement::ExecuteInternal(FPCGContext* Context) const
{
	// Essentially a pass-through element
	Context->OutputData = Context->InputData;
	return true;
}

UPCGGraphInputOutputSettings::UPCGGraphInputOutputSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	StaticInLabels.Add(PCGInputOutputConstants::DefaultInLabel);
	StaticAdvancedInLabels.Add(PCGInputOutputConstants::DefaultInputLabel);
	StaticAdvancedInLabels.Add(PCGInputOutputConstants::DefaultActorLabel);
	StaticAdvancedInLabels.Add(PCGInputOutputConstants::DefaultOriginalActorLabel);
	StaticAdvancedInLabels.Add(PCGInputOutputConstants::DefaultExcludedActorsLabel);
	
	StaticOutLabels.Add(PCGInputOutputConstants::DefaultOutLabel);
}