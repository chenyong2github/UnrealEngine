// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factories/Experimental/ChaosPhysicalMaterialFactory.h"

#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Selection.h"

#define LOCTEXT_NAMESPACE "ChaosPhysicalMaterial"

/////////////////////////////////////////////////////
// ChaosPhysicalMaterialFactory

UChaosPhysicalMaterialFactory::UChaosPhysicalMaterialFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UChaosPhysicalMaterial::StaticClass();
}

UChaosPhysicalMaterial* UChaosPhysicalMaterialFactory::StaticFactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UChaosPhysicalMaterial* System = static_cast<UChaosPhysicalMaterial*>(NewObject<UChaosPhysicalMaterial>(InParent, Class, Name, Flags | RF_Transactional | RF_Public | RF_Standalone));
	System->MarkPackageDirty();
	return System;
}

UObject* UChaosPhysicalMaterialFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UChaosPhysicalMaterial* NewChaosPhysicalMaterial = StaticFactoryCreateNew(Class, InParent, Name, Flags, Context, Warn);
	NewChaosPhysicalMaterial->MarkPackageDirty();
	return NewChaosPhysicalMaterial;
}

#undef LOCTEXT_NAMESPACE
