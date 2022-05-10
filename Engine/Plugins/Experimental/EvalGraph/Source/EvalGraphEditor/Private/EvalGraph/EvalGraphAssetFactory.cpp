// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraphAssetFactory.h"

#include "EvalGraph/EvalGraphObject.h"

UEvalGraphAssetFactory::UEvalGraphAssetFactory()
{
	SupportedClass = UEvalGraph::StaticClass();
}

bool UEvalGraphAssetFactory::CanCreateNew() const
{
	return true;
}

bool UEvalGraphAssetFactory::FactoryCanImport(const FString& Filename)
{
	return false;
}

UObject* UEvalGraphAssetFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
												   UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UEvalGraph>(InParent, InClass, InName, Flags);
}

bool UEvalGraphAssetFactory::ShouldShowInNewMenu() const
{
	return true;
}

bool UEvalGraphAssetFactory::ConfigureProperties()
{
	return true;
}
