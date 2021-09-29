// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFeatureAction_AddCheats.h"
#include "GameFramework/CheatManager.h"

#define LOCTEXT_NAMESPACE "GameFeatures"

//////////////////////////////////////////////////////////////////////
// UGameFeatureAction_AddCheats

void UGameFeatureAction_AddCheats::OnGameFeatureActivating()
{
	CheatManagerRegistrationHandle = UCheatManager::RegisterForOnCheatManagerCreated(FOnCheatManagerCreated::FDelegate::CreateUObject(this, &ThisClass::OnCheatManagerCreated));
}

void UGameFeatureAction_AddCheats::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
	UCheatManager::UnregisterFromOnCheatManagerCreated(CheatManagerRegistrationHandle);

	for (TWeakObjectPtr<UCheatManagerExtension> ExtensionPtr : SpawnedCheatManagers)
	{
		if (UCheatManagerExtension* Extension = ExtensionPtr.Get())
		{
			UCheatManager* CheatManager = CastChecked<UCheatManager>(Extension->GetOuter());
			CheatManager->RemoveCheatManagerExtension(Extension);
		}
	}
	SpawnedCheatManagers.Empty();
}

#if WITH_EDITOR
EDataValidationResult UGameFeatureAction_AddCheats::IsDataValid(TArray<FText>& ValidationErrors)
{
	EDataValidationResult Result = CombineDataValidationResults(Super::IsDataValid(ValidationErrors), EDataValidationResult::Valid);

	int32 EntryIndex = 0;
	for (const TSubclassOf<UCheatManagerExtension> CheatManagerClass : CheatManagers)
	{
		if (!CheatManagerClass)
		{
			Result = EDataValidationResult::Invalid;
			ValidationErrors.Add(FText::Format(LOCTEXT("CheatEntryIsNull", "Null entry at index {0} in CheatManagers"), FText::AsNumber(EntryIndex)));
		}
		++EntryIndex;
	}

	return Result;
}
#endif

void UGameFeatureAction_AddCheats::OnCheatManagerCreated(UCheatManager* CheatManager)
{
	// First clean out any stale pointers
	for (int32 ManagerIdx = SpawnedCheatManagers.Num() - 1; ManagerIdx >= 0; --ManagerIdx)
	{
		if (!SpawnedCheatManagers[ManagerIdx].IsValid())
		{
			SpawnedCheatManagers.RemoveAtSwap(ManagerIdx);
		}
	}

	for (TSubclassOf<UCheatManagerExtension> CheatManagerClass : CheatManagers)
	{
		if (CheatManagerClass != nullptr)
		{
			if ((CheatManagerClass->ClassWithin == nullptr) || CheatManager->IsA(CheatManagerClass->ClassWithin))
			{
				UCheatManagerExtension* Extension = NewObject<UCheatManagerExtension>(CheatManager, CheatManagerClass);
				SpawnedCheatManagers.Add(Extension);
				CheatManager->AddCheatManagerExtension(Extension);
			}
		}
	}
};

//////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
