// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassSchematicFactory.h"
#include "MassSchematic.h"

#define LOCTEXT_NAMESPACE "MassSchematicFactory"

UMassSchematicFactory::UMassSchematicFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UMassSchematic::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

bool UMassSchematicFactory::CanCreateNew() const
{
	return true;
}

UObject* UMassSchematicFactory::FactoryCreateNew(UClass* Class,UObject* InParent,FName Name,EObjectFlags Flags,UObject* Context,FFeedbackContext* Warn)
{
	check(Class->IsChildOf(UMassSchematic::StaticClass()));
	return NewObject<UMassSchematic>(InParent, Class, Name, Flags);
}

#undef LOCTEXT_NAMESPACE
