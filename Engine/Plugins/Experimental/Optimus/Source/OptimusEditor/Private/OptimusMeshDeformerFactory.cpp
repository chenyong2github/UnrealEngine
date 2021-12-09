// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusMeshDeformerFactory.h"

#include "AssetTypeCategories.h"
#include "OptimusMeshDeformer.h"


UOptimusMeshDeformerFactory::UOptimusMeshDeformerFactory()
{
	SupportedClass = UOptimusMeshDeformer::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UOptimusMeshDeformerFactory::FactoryCreateNew(
	UClass* InClass, 
	UObject* InParent, 
	FName InName, 
	EObjectFlags InFlags, 
	UObject* InContext, 
	FFeedbackContext* OutWarn
	)
{
	return NewObject<UOptimusMeshDeformer>(InParent, InClass, InName, InFlags);
}

uint32 UOptimusMeshDeformerFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Animation;
}

bool UOptimusMeshDeformerFactory::ShouldShowInNewMenu() const
{
	return true;
}
