// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassSchematicFactory.h"
#include "MassSchematic.h"

#define LOCTEXT_NAMESPACE "PipeSchematicFactory"

UPipeSchematicFactory::UPipeSchematicFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UPipeSchematic::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

bool UPipeSchematicFactory::CanCreateNew() const
{
	return true;
}

UObject* UPipeSchematicFactory::FactoryCreateNew(UClass* Class,UObject* InParent,FName Name,EObjectFlags Flags,UObject* Context,FFeedbackContext* Warn)
{
	check(Class->IsChildOf(UPipeSchematic::StaticClass()));
	return NewObject<UPipeSchematic>(InParent, Class, Name, Flags);
}

#undef LOCTEXT_NAMESPACE
