// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusTestGraphFactory.h"
#include "OptimusTestGraph.h"

UOptimusTestGraphFactory::UOptimusTestGraphFactory()
{
	SupportedClass = UOptimusTestGraph::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UOptimusTestGraphFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UOptimusTestGraph>(InParent, InClass, InName, Flags);
}
