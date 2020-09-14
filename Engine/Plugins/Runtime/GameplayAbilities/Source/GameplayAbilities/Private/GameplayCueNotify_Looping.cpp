// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayCueNotify_Looping.h"


//////////////////////////////////////////////////////////////////////////
// AGameplayCueNotify_Looping
//////////////////////////////////////////////////////////////////////////
AGameplayCueNotify_Looping::AGameplayCueNotify_Looping()
{
	bAllowMultipleWhileActiveEvents = false;

	Recycle();
}

void AGameplayCueNotify_Looping::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (EndPlayReason == EEndPlayReason::Destroyed)
	{
		RemoveLoopingEffects();
	}

	Super::EndPlay(EndPlayReason);
}

bool AGameplayCueNotify_Looping::Recycle()
{
	Super::Recycle();

	// Extra check to make sure looping effects have been removed.  Normally they will have been removed in the OnRemove event.
	RemoveLoopingEffects();

	ApplicationSpawnResults.Reset();
	LoopingSpawnResults.Reset();
	RecurringSpawnResults.Reset();
	RemovalSpawnResults.Reset();

	bLoopingEffectsRemoved = false;

	return true;
}

bool AGameplayCueNotify_Looping::OnActive_Implementation(AActor* Target, const FGameplayCueParameters& Parameters)
{
	UWorld* World = GetWorld();

	const FGameplayCueNotify_LoopingSparseData* SparseData = GetGameplayCueNotify_LoopingSparseData();
	check(SparseData);

	FGameplayCueNotify_SpawnContext SpawnContext(World, Target, Parameters);
	SpawnContext.SetDefaultSpawnCondition(&SparseData->DefaultSpawnCondition);
	SpawnContext.SetDefaultPlacementInfo(&SparseData->DefaultPlacementInfo);

	if (SparseData->DefaultSpawnCondition.ShouldSpawn(SpawnContext))
	{
		SparseData->ApplicationEffects.ExecuteEffects(SpawnContext, ApplicationSpawnResults);

		OnApplication(Target, Parameters, ApplicationSpawnResults);
	}

	return false;
}

bool AGameplayCueNotify_Looping::WhileActive_Implementation(AActor* Target, const FGameplayCueParameters& Parameters)
{
	UWorld* World = GetWorld();

	const FGameplayCueNotify_LoopingSparseData* SparseData = GetGameplayCueNotify_LoopingSparseData();
	check(SparseData);

	FGameplayCueNotify_SpawnContext SpawnContext(World, Target, Parameters);
	SpawnContext.SetDefaultSpawnCondition(&SparseData->DefaultSpawnCondition);
	SpawnContext.SetDefaultPlacementInfo(&SparseData->DefaultPlacementInfo);

	if (SparseData->DefaultSpawnCondition.ShouldSpawn(SpawnContext))
	{
		bLoopingEffectsRemoved = false;

		SparseData->LoopingEffects.StartEffects(SpawnContext, LoopingSpawnResults);	

		OnLoopingStart(Target, Parameters, LoopingSpawnResults);
	}

	return false;
}

bool AGameplayCueNotify_Looping::OnExecute_Implementation(AActor* Target, const FGameplayCueParameters& Parameters)
{
	UWorld* World = GetWorld();

	const FGameplayCueNotify_LoopingSparseData* SparseData = GetGameplayCueNotify_LoopingSparseData();
	check(SparseData);

	FGameplayCueNotify_SpawnContext SpawnContext(World, Target, Parameters);
	SpawnContext.SetDefaultSpawnCondition(&SparseData->DefaultSpawnCondition);
	SpawnContext.SetDefaultPlacementInfo(&SparseData->DefaultPlacementInfo);

	if (SparseData->DefaultSpawnCondition.ShouldSpawn(SpawnContext))
	{
		SparseData->RecurringEffects.ExecuteEffects(SpawnContext, RecurringSpawnResults);

		OnRecurring(Target, Parameters, RecurringSpawnResults);
	}

	return false;
}

bool AGameplayCueNotify_Looping::OnRemove_Implementation(AActor* Target, const FGameplayCueParameters& Parameters)
{
	RemoveLoopingEffects();

	// Don't spawn removal effects if our target is gone.
	if (Target)
	{
		UWorld* World = GetWorld();

		const FGameplayCueNotify_LoopingSparseData* SparseData = GetGameplayCueNotify_LoopingSparseData();
		check(SparseData);

		FGameplayCueNotify_SpawnContext SpawnContext(World, Target, Parameters);
		SpawnContext.SetDefaultSpawnCondition(&SparseData->DefaultSpawnCondition);
		SpawnContext.SetDefaultPlacementInfo(&SparseData->DefaultPlacementInfo);

		if (SparseData->DefaultSpawnCondition.ShouldSpawn(SpawnContext))
		{
			SparseData->RemovalEffects.ExecuteEffects(SpawnContext, RemovalSpawnResults);
		}
	}

	// Always call OnRemoval(), even if target is bad, so it can clean up BP-spawned things.
	OnRemoval(Target, Parameters, RemovalSpawnResults);

	return false;
}

void AGameplayCueNotify_Looping::RemoveLoopingEffects()
{
	if (bLoopingEffectsRemoved)
	{
		return;
	}

	const FGameplayCueNotify_LoopingSparseData* SparseData = GetGameplayCueNotify_LoopingSparseData();
	check(SparseData);

	bLoopingEffectsRemoved = true;

	SparseData->LoopingEffects.StopEffects(LoopingSpawnResults);
}

#if WITH_EDITOR
EDataValidationResult AGameplayCueNotify_Looping::IsDataValid(TArray<FText>& ValidationErrors)
{
	const FGameplayCueNotify_LoopingSparseData* SparseData = GetGameplayCueNotify_LoopingSparseData();
	check(SparseData);

	SparseData->ApplicationEffects.ValidateAssociatedAssets(this, TEXT("ApplicationEffects"), ValidationErrors);
	SparseData->LoopingEffects.ValidateAssociatedAssets(this, TEXT("LoopingEffects"), ValidationErrors);
	SparseData->RecurringEffects.ValidateAssociatedAssets(this, TEXT("RecurringEffects"), ValidationErrors);
	SparseData->RemovalEffects.ValidateAssociatedAssets(this, TEXT("RemovalEffects"), ValidationErrors);

	return ((ValidationErrors.Num() > 0) ? EDataValidationResult::Invalid : EDataValidationResult::Valid);
}
#endif // #if WITH_EDITOR
