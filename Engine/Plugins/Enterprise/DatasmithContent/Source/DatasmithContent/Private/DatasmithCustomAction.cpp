// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithCustomAction.h"

#include "Misc/Paths.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"


FDatasmithCustomActionManager::FDatasmithCustomActionManager()
{
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (!It->HasAnyClassFlags(CLASS_Abstract) && It->IsChildOf(UDatasmithCustomActionBase::StaticClass()))
		{
			UDatasmithCustomActionBase* ActionCDO = It->GetDefaultObject<UDatasmithCustomActionBase>();
			RegisteredActions.Emplace(ActionCDO);
		}
	}
}

TArray<UDatasmithCustomActionBase*> FDatasmithCustomActionManager::GetApplicableActions(const TArray<FAssetData>& SelectedAssets)
{
	TArray<UDatasmithCustomActionBase*> ApplicableActions;

	for (const TStrongObjectPtr<UDatasmithCustomActionBase>& Action : RegisteredActions)
	{
		if (Action.IsValid() && Action->CanApplyOnAssets(SelectedAssets))
		{
			ApplicableActions.Add(Action.Get());
		}
	}

	return ApplicableActions;
}

TArray<UDatasmithCustomActionBase*> FDatasmithCustomActionManager::GetApplicableActions(const TArray<AActor*>& SelectedActors)
{
	TArray<UDatasmithCustomActionBase*> ApplicableActions;

	for (const TStrongObjectPtr<UDatasmithCustomActionBase>& Action : RegisteredActions)
	{
		if (Action.IsValid() && Action->CanApplyOnActors(SelectedActors))
		{
			ApplicableActions.Add(Action.Get());
		}
	}

	return ApplicableActions;
}

