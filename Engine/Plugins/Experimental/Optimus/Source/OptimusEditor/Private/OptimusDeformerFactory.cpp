// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusDeformerFactory.h"

#include "AssetTypeCategories.h"
#include "OptimusDeformer.h"


UOptimusDeformerFactory::UOptimusDeformerFactory()
{
	SupportedClass = UOptimusDeformer::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UOptimusDeformerFactory::FactoryCreateNew(
	UClass* InClass, 
	UObject* InParent, 
	FName InName, 
	EObjectFlags InFlags, 
	UObject* InContext, 
	FFeedbackContext* OutWarn
	)
{
	return NewObject<UOptimusDeformer>(InParent, InClass, InName, InFlags);
}


uint32 UOptimusDeformerFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Animation;
}


bool UOptimusDeformerFactory::ShouldShowInNewMenu() const
{
	return true;
}
