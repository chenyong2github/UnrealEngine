// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosFlesh/Asset/ActorFactoryDeformableSolver.h"

#include "ChaosFlesh/ChaosDeformableSolverActor.h"
#include "ChaosFlesh/ChaosDeformableSolverAsset.h"
#include "AssetRegistry/AssetData.h"

#define LOCTEXT_NAMESPACE "ActorFactoryDeformableSolver"


/*-----------------------------------------------------------------------------
UActorFactoryDeformableSolver
-----------------------------------------------------------------------------*/
UActorFactoryDeformableSolver::UActorFactoryDeformableSolver(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DisplayName = LOCTEXT("DeformableSolverDisplayName", "DeformableSolver");
	NewActorClass = ADeformableSolverActor::StaticClass();
}

bool UActorFactoryDeformableSolver::CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg)
{
	if (!AssetData.IsValid() || !AssetData.IsInstanceOf(UChaosDeformableSolver::StaticClass()))
	{
		OutErrorMsg = NSLOCTEXT("CanCreateActor", "NoDeformableSolverSpecified", "No solver asset was specified.");
		return false;
	}

	return true;
}

void UActorFactoryDeformableSolver::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	Super::PostSpawnActor(Asset, NewActor);

	UChaosDeformableSolver* ChaosSolver = CastChecked<UChaosDeformableSolver>(Asset);
	ADeformableSolverActor * NewDeformableSolverActo = CastChecked<ADeformableSolverActor >(NewActor);
}

void UActorFactoryDeformableSolver::PostCreateBlueprint(UObject* Asset, AActor* CDO)
{
	if (Asset != NULL && CDO != NULL)
	{
		UChaosDeformableSolver* ChaosSolver = CastChecked<UChaosDeformableSolver>(Asset);
		ADeformableSolverActor* ChaosSolverActor = CastChecked<ADeformableSolverActor>(CDO);
	}
}

#undef LOCTEXT_NAMESPACE
