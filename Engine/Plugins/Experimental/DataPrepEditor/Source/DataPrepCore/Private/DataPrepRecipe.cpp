// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataPrepRecipe.h"

#include "ActorEditorUtils.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "UObject/SoftObjectPath.h"

#define LOCTEXT_NAMESPACE "DataprepRecipe"

namespace DataprepRecipeUtils
{
	bool IsActorValid(const AActor* Actor)
	{
		if (Actor == nullptr)
		{
			return false;
		}

		// Don't consider transient actors in non-play worlds
		// Don't consider the builder brush
		// Don't consider the WorldSettings actor, even though it is technically editable
		bool bIsValid = Actor->IsEditable() && !Actor->IsTemplate() && !Actor->HasAnyFlags(RF_Transient) && !FActorEditorUtils::IsABuilderBrush(Actor) && !Actor->IsA(AWorldSettings::StaticClass());

		if (Actor->GetWorld() == GWorld)
		{
			// Only consider actors that are allowed to be selected and drawn in editor
			bIsValid &= Actor->IsListedInSceneOutliner();
		}

		return bIsValid;
	}
}

UDataprepRecipe::UDataprepRecipe()
{
	TargetWorld = nullptr;
}

TArray<TWeakObjectPtr<UObject>> UDataprepRecipe::GetValidAssets(bool bFlushAssets /*= true*/)
{
	TArray<TWeakObjectPtr<UObject>> ValidAssets;

#if WITH_EDITOR
	for (const TWeakObjectPtr<UObject>& AssetPtr : Assets)
	{
		if (AssetPtr.Get() != nullptr && !AssetPtr->IsPendingKill())
		{
			ValidAssets.Add(AssetPtr);
		}
	}

	// Empty data prep recipe's list of assets
	if (bFlushAssets == true)
	{
		Assets.Empty();
	}
#endif

	return ValidAssets;
}

TArray<AActor*> UDataprepRecipe::GetActors() const
{
	TArray<AActor*> Result;

	if (TargetWorld == nullptr)
	{
		return Result;
	}

#if WITH_EDITORONLY_DATA
	const EActorIteratorFlags Flags = EActorIteratorFlags::SkipPendingKill;
	for (TActorIterator<AActor> It(TargetWorld, AActor::StaticClass(), Flags); It; ++It)
	{
		AActor* Actor = *It;
		if (DataprepRecipeUtils::IsActorValid(Actor))
		{
			Result.Add(Actor);
		}
	}
#endif

	return Result;
}

TArray<UObject*> UDataprepRecipe::GetAssets() const
{
	TArray<UObject*> Result;

	for (const TWeakObjectPtr<UObject>& AssetPtr : Assets)
	{
		if (AssetPtr.Get() != nullptr && !AssetPtr->IsPendingKill())
		{
			Result.Add(AssetPtr.Get());
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
