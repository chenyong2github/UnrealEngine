// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotFactory.h"
#include "LevelSnapshot.h"

ULevelSnapshotFactory::ULevelSnapshotFactory()
{
	SupportedClass = ULevelSnapshot::StaticClass();
};

UObject* ULevelSnapshotFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<ULevelSnapshot>(InParent, InClass, InName, Flags);
};


bool ULevelSnapshotFactory::ShouldShowInNewMenu() const
{
	return false;
};