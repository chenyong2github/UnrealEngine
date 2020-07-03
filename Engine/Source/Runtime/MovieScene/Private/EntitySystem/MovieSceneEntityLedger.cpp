// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntitySystem/MovieSceneEntityLedger.h"
#include "EntitySystem/MovieSceneInstanceRegistry.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/BuiltInComponentTypes.h"

#include "Evaluation/MovieSceneEvaluationField.h"

#include "MovieSceneSection.h"

namespace UE
{
namespace MovieScene
{


ESequenceUpdateResult FEntityLedger::UpdateEntities(UMovieSceneEntitySystemLinker* Linker, const FEntityImportSequenceParams& ImportParams, const FMovieSceneEntityComponentField* EntityField, const TSet<FMovieSceneEvaluationFieldEntityPtr>& NewEntities)
{
	ESequenceUpdateResult Result = ESequenceUpdateResult::NoChange;

	FInstanceRegistry* InstanceRegistry = Linker->GetInstanceRegistry();
	if (NewEntities.Num() != 0)
	{
		// Destroy any entities that are no longer relevant
		if (ImportedEntities.Num() != 0)
		{
			FComponentMask FinishedMask = FBuiltInComponentTypes::Get()->FinishedMask;

			for (auto It = ImportedEntities.CreateIterator(); It; ++It)
			{
				if (!NewEntities.Contains(It.Key()))
				{
					if (It.Value())
					{
						Linker->EntityManager.AddComponents(It.Value(), FinishedMask, EEntityRecursion::Full);

						Result |= ESequenceUpdateResult::EntitiesDirty;
					}
					It.RemoveCurrent();
				}
			}
		}

		// If we've invalidated or we haven't imported anything yet, we can simply (re)import everything
		if (ImportedEntities.Num() == 0 || bInvalidated)
		{
			for (FMovieSceneEvaluationFieldEntityPtr Entity : NewEntities)
			{
				Result |= ImportEntity(Linker, ImportParams, EntityField, Entity);
			}
		}
		else for (FMovieSceneEvaluationFieldEntityPtr Entity : NewEntities)
		{
			if (!HasImportedEntity(Entity))
			{
				Result |= ImportEntity(Linker, ImportParams, EntityField, Entity);
			}
		}
	}
	else
	{
		Result |= UnlinkEverything(Linker);
	}

	// Nothing is invalidated now
	bInvalidated = false;

	return Result;
}

ESequenceUpdateResult FEntityLedger::UpdateOneShotEntities(UMovieSceneEntitySystemLinker* Linker, const FEntityImportSequenceParams& ImportParams, const FMovieSceneEntityComponentField* EntityField, const TSet<FMovieSceneEvaluationFieldEntityPtr>& NewEntities)
{
	ESequenceUpdateResult Result = ESequenceUpdateResult::NoChange;

	checkf(OneShotEntities.Num() == 0, TEXT("One shot entities should not be updated multiple times per-evaluation. They must not have gotten cleaned up correctly."));
	if (NewEntities.Num() == 0)
	{
		return Result;
	}

	FEntityImportParams Params;
	Params.Sequence = ImportParams;

	for (FMovieSceneEvaluationFieldEntityPtr Entity : NewEntities)
	{
		IMovieSceneEntityProvider* Provider = Cast<IMovieSceneEntityProvider>(Entity.EntityOwner);
		if (!Provider)
		{
			continue;
		}

		Params.EntityID = Entity.EntityID;
		if (EntityField)
		{
			Params.ObjectBindingID = EntityField->EntityOwnerToObjectBinding.FindRef(Entity.EntityOwner);
		}

		FImportedEntity ImportedEntity;
		Result |= Provider->ImportEntity(Linker, Params, &ImportedEntity);

		if (!ImportedEntity.IsEmpty())
		{
			if (UMovieSceneSection* Section = Cast<UMovieSceneSection>(Entity.EntityOwner))
			{
				Section->BuildDefaultComponents(Linker, Params, &ImportedEntity);
			}

			FMovieSceneEntityID NewEntityID = ImportedEntity.Manufacture(Params, &Linker->EntityManager);

			OneShotEntities.Add(NewEntityID);
			Result |= ESequenceUpdateResult::EntitiesDirty;
		}
	}

	return Result;
}

void FEntityLedger::Invalidate()
{
	bInvalidated = true;
}

bool FEntityLedger::IsEmpty() const
{
	return ImportedEntities.Num() == 0;
}

bool FEntityLedger::HasImportedEntity(const FMovieSceneEvaluationFieldEntityPtr& Entity) const
{
	return ImportedEntities.Contains(Entity);
}

FMovieSceneEntityID FEntityLedger::FindImportedEntity(const FMovieSceneEvaluationFieldEntityPtr& Entity) const
{
	return ImportedEntities.FindRef(Entity);
}

ESequenceUpdateResult FEntityLedger::ImportEntity(UMovieSceneEntitySystemLinker* Linker, const FEntityImportSequenceParams& ImportParams, const FMovieSceneEntityComponentField* EntityField, const FMovieSceneEvaluationFieldEntityPtr& Entity)
{
	check(!HasImportedEntity(Entity) || bInvalidated);

	// We always add an entry even if no entity was imported by the provider to ensure that we do not repeatedly try and import the same entity every frame
	FMovieSceneEntityID& RefEntityID = ImportedEntities.FindOrAdd(Entity);

	IMovieSceneEntityProvider* Provider = Cast<IMovieSceneEntityProvider>(Entity.EntityOwner);
	if (!Provider)
	{
		return ESequenceUpdateResult::NoChange;
	}

	FEntityImportParams Params;
	Params.Sequence = ImportParams;
	Params.EntityID = Entity.EntityID;
	if (EntityField)
	{
		Params.ObjectBindingID = EntityField->EntityOwnerToObjectBinding.FindRef(Entity.EntityOwner);
	}

	FImportedEntity ImportedEntity;
	ESequenceUpdateResult Result = Provider->ImportEntity(Linker, Params, &ImportedEntity);

	if (!ImportedEntity.IsEmpty())
	{
		if (UMovieSceneSection* Section = Cast<UMovieSceneSection>(Entity.EntityOwner))
		{
			Section->BuildDefaultComponents(Linker, Params, &ImportedEntity);
		}

		FMovieSceneEntityID NewEntityID = ImportedEntity.Manufacture(Params, &Linker->EntityManager);

		Linker->EntityManager.ReplaceEntityID(RefEntityID, NewEntityID);
		Result |= ESequenceUpdateResult::EntitiesDirty;
	}

	return Result;
}

ESequenceUpdateResult FEntityLedger::UnlinkEverything(UMovieSceneEntitySystemLinker* Linker)
{
	ESequenceUpdateResult Result = ESequenceUpdateResult::NoChange;

	FComponentMask FinishedMask = FBuiltInComponentTypes::Get()->FinishedMask;

	for (TPair<FMovieSceneEvaluationFieldEntityPtr, FMovieSceneEntityID>& Pair : ImportedEntities)
	{
		if (Pair.Value)
		{
			Linker->EntityManager.AddComponents(Pair.Value, FinishedMask, EEntityRecursion::Full);
			Result = ESequenceUpdateResult::EntitiesDirty;
		}
	}
	ImportedEntities.Empty();

	return Result;
}

void FEntityLedger::UnlinkOneShots(UMovieSceneEntitySystemLinker* Linker)
{
	FComponentMask FinishedMask = FBuiltInComponentTypes::Get()->FinishedMask;

	for (FMovieSceneEntityID Entity : OneShotEntities)
	{
		Linker->EntityManager.AddComponents(Entity, FinishedMask, EEntityRecursion::Full);
	}
	OneShotEntities.Empty();
}

void FEntityLedger::CleanupLinkerEntities(const TSet<FMovieSceneEntityID>& LinkerEntities)
{
	for (int32 Index = OneShotEntities.Num()-1; Index >= 0; --Index)
	{
		if (LinkerEntities.Contains(OneShotEntities[Index]))
		{
			OneShotEntities.RemoveAtSwap(Index, 1, false);
		}
	}
	for (auto It = ImportedEntities.CreateIterator(); It; ++It)
	{
		if (It.Value() && LinkerEntities.Contains(It.Value()))
		{
			It.RemoveCurrent();
		}
	}
}

void FEntityLedger::TagGarbage(UMovieSceneEntitySystemLinker* Linker)
{
	FComponentTypeID NeedsUnlink = FBuiltInComponentTypes::Get()->Tags.NeedsUnlink;

	for (auto It = ImportedEntities.CreateIterator(); It; ++It)
	{
		if (It.Key().EntityOwner == nullptr)
		{
			if (It.Value())
			{
				Linker->EntityManager.AddComponent(It.Value(), NeedsUnlink, EEntityRecursion::Full);
			}
			It.RemoveCurrent();
		}
	}
}

} // namespace MovieScene
} // namespace UE