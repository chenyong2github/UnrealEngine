// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGActorSelector.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGActorHelpers.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGActorSelector)

namespace PCGActorSelector
{
	// Need to pass a pointer of pointer to the found actor. The lambda will capture this pointer and modify its value when an actor is found.
	TFunction<bool(AActor*)> GetFilteringFunction(const FPCGActorSelectorSettings& InSettings, TArray<AActor*>& InFoundActors)
	{
		const bool bMultiSelect = InSettings.bSelectMultiple;

		switch (InSettings.ActorSelection)
		{
		case EPCGActorSelection::ByTag:
			return[ActorSelectionTag = InSettings.ActorSelectionTag, &InFoundActors, bMultiSelect](AActor* Actor) -> bool
			{
				if (Actor->ActorHasTag(ActorSelectionTag))
				{
					InFoundActors.Add(Actor);
					return bMultiSelect;
				}

				return true;
			};

		case EPCGActorSelection::ByName:
			return[ActorSelectionName = InSettings.ActorSelectionName, &InFoundActors, bMultiSelect](AActor* Actor) -> bool
			{
				if (Actor->GetFName().IsEqual(ActorSelectionName, ENameCase::IgnoreCase, /*bCompareNumber=*/ false))
				{
					InFoundActors.Add(Actor);
					return bMultiSelect;
				}

				return true;
			};

		case EPCGActorSelection::ByClass:
			return[ActorSelectionClass = InSettings.ActorSelectionClass, &InFoundActors, bMultiSelect](AActor* Actor) -> bool
			{
				if (Actor->IsA(ActorSelectionClass))
				{
					InFoundActors.Add(Actor);
					return bMultiSelect;
				}

				return true;
			};

		default:
			break;
		}

		return [](AActor* Actor) -> bool { return false; };
	}

	TArray<AActor*> FindActors(const FPCGActorSelectorSettings& Settings, UWorld* World, AActor* Self)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGActorSelector::FindActor);

		TArray<AActor*> FoundActors;

		if (!World)
		{
			return FoundActors;
		}

		// Early out if we have not the information necessary
		if ((Settings.ActorSelection == EPCGActorSelection::ByTag && Settings.ActorSelectionTag == NAME_None) ||
			(Settings.ActorSelection == EPCGActorSelection::ByName && Settings.ActorSelectionName == NAME_None) ||
			(Settings.ActorSelection == EPCGActorSelection::ByClass && !Settings.ActorSelectionClass))
		{
			return FoundActors;
		}

		// We pass FoundActor ref, that will be captured by the FilteringFunction
		// It will modify the FoundActor pointer to the found actor, if found.
		TFunction<bool(AActor*)> FilteringFunction = PCGActorSelector::GetFilteringFunction(Settings, FoundActors);

		// In case of iterating over all actors in the world, call our filtering function and get out.
		if (Settings.ActorFilter == EPCGActorFilter::AllWorldActors)
		{
			UPCGActorHelpers::ForEachActorInWorld<AActor>(World, FilteringFunction);

			// FoundActor is set by the FilteringFunction (captured)
			return FoundActors;
		}

		// Otherwise, gather all the actors we need to check
		TArray<AActor*> ActorsToCheck;
		switch (Settings.ActorFilter)
		{
		case EPCGActorFilter::Self:
			if (Self)
			{
				ActorsToCheck.Add(Self);
			}
			break;

		case EPCGActorFilter::Parent:
			if (Self)
			{
				if (AActor* Parent = Self->GetParentActor())
				{
					ActorsToCheck.Add(Parent);
				}
				else
				{
					// If there is no parent, set the owner as the parent.
					ActorsToCheck.Add(Self);
				}
			}
			break;

		case EPCGActorFilter::Root:
		{
			AActor* Current = Self;
			while (Current != nullptr)
			{
				AActor* Parent = Current->GetParentActor();
				if (Parent == nullptr)
				{
					ActorsToCheck.Add(Current);
					break;
				}
				Current = Parent;
			}

			break;
		}

		//case EPCGActorFilter::TrackedActors:
			//	//TODO
			//	break;

		default:
			break;
		}

		if (Settings.bIncludeChildren)
		{
			const int32 InitialCount = ActorsToCheck.Num();
			for (int32 i = 0; i < InitialCount; ++i)
			{
				ActorsToCheck[i]->GetAttachedActors(ActorsToCheck, /*bResetArray=*/ false, /*bRecursivelyIncludeAttachedActors=*/ true);
			}
		}

		for (AActor* Actor : ActorsToCheck)
		{
			// FoundActor is set by the FilteringFunction (captured)
			if (!FilteringFunction(Actor))
			{
				break;
			}
		}

		return FoundActors;
	}

	AActor* FindActor(const FPCGActorSelectorSettings& InSettings, UWorld* World, AActor* Self)
	{
		// In order to make sure we don't try to select multiple, we'll do a copy of the settings here.
		FPCGActorSelectorSettings Settings = InSettings;
		Settings.bSelectMultiple = false;

		TArray<AActor*> Actors = FindActors(Settings, World, Self);
		return Actors.IsEmpty() ? nullptr : Actors[0];
	}
}
