// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosFlesh/Asset/ChaosAssetDeformableSolverFactory.h"

#include "ChaosFlesh/ChaosDeformableSolverAsset.h"

#define LOCTEXT_NAMESPACE "ChaosDeformableSolver"

/////////////////////////////////////////////////////
// ChaosDeformableSolverFactory

UChaosDeformableSolverFactory::UChaosDeformableSolverFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UChaosDeformableSolver::StaticClass();
}

UChaosDeformableSolver* UChaosDeformableSolverFactory::StaticFactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return static_cast<UChaosDeformableSolver*>(NewObject<UChaosDeformableSolver>(InParent, Class, Name, Flags | RF_Transactional | RF_Public | RF_Standalone));
}

UObject* UChaosDeformableSolverFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UChaosDeformableSolver* NewChaosDeformableSolver = StaticFactoryCreateNew(Class, InParent, Name, Flags, Context, Warn);
	NewChaosDeformableSolver->MarkPackageDirty();
	return NewChaosDeformableSolver;
}

#undef LOCTEXT_NAMESPACE



