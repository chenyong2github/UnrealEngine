// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowAssetFactory.h"

#include "Dataflow/DataflowObject.h"
#include "HAL/IConsoleManager.h"

bool bDataflowEnableCreation = false;
FAutoConsoleVariableRef CVarDataflowEnableCreation(TEXT("c.DataflowEnableCreation"), bDataflowEnableCreation, TEXT("Enable creation for the dataflow system (Curently Dev-Only)"));


UDataflowAssetFactory::UDataflowAssetFactory()
{
	SupportedClass = UDataflow::StaticClass();
}

bool UDataflowAssetFactory::CanCreateNew() const
{
	return bDataflowEnableCreation;
}

bool UDataflowAssetFactory::FactoryCanImport(const FString& Filename)
{
	return false;
}

UObject* UDataflowAssetFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
												   UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UDataflow>(InParent, InClass, InName, Flags);
}

bool UDataflowAssetFactory::ShouldShowInNewMenu() const
{
	return bDataflowEnableCreation;
}

bool UDataflowAssetFactory::ConfigureProperties()
{
	return true;
}
