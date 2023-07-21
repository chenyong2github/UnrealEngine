// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGActorSelector.h"

#include "PCGComponent.h"
#include "PCGModule.h"
#include "Grid/PCGPartitionActor.h"
#include "Helpers/PCGActorHelpers.h"

#include "GameFramework/Actor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGActorSelector)

namespace PCGActorSelector
{
	// Filter is required if it is not disabled and if we are gathering all world actors or gathering all children.
	bool FilterRequired(const FPCGActorSelectorSettings& InSettings)
	{
		return (InSettings.ActorFilter == EPCGActorFilter::AllWorldActors || InSettings.bIncludeChildren) && !InSettings.bDisableFilter;
	}

	// Need to pass a pointer of pointer to the found actor. The lambda will capture this pointer and modify its value when an actor is found.
	TFunction<bool(AActor*)> GetFilteringFunction(const FPCGActorSelectorSettings& InSettings, const TFunction<bool(const AActor*)>& BoundsCheck, const TFunction<bool(const AActor*)>& SelfIgnoreCheck, TArray<AActor*>& InFoundActors)
	{
		if (!FilterRequired(InSettings))
		{
			return [&InFoundActors, &BoundsCheck, &SelfIgnoreCheck](AActor* Actor) -> bool
			{
				if (BoundsCheck(Actor) && SelfIgnoreCheck(Actor))
				{
					InFoundActors.Add(Actor);
				}
				return true;
			};
		}

		const bool bMultiSelect = InSettings.bSelectMultiple;

		switch (InSettings.ActorSelection)
		{
		case EPCGActorSelection::ByTag:
			return[ActorSelectionTag = InSettings.ActorSelectionTag, &InFoundActors, bMultiSelect, &BoundsCheck, &SelfIgnoreCheck](AActor* Actor) -> bool
			{
				if (Actor && Actor->ActorHasTag(ActorSelectionTag) && BoundsCheck(Actor) && SelfIgnoreCheck(Actor))
				{
					InFoundActors.Add(Actor);
					return bMultiSelect;
				}

				return true;
			};

		case EPCGActorSelection::ByClass:
			return[ActorSelectionClass = InSettings.ActorSelectionClass, &InFoundActors, bMultiSelect, &BoundsCheck, &SelfIgnoreCheck](AActor* Actor) -> bool
			{
				if (Actor && Actor->IsA(ActorSelectionClass) && BoundsCheck(Actor) && SelfIgnoreCheck(Actor))
				{
					InFoundActors.Add(Actor);
					return bMultiSelect;
				}

				return true;
			};

		case EPCGActorSelection::ByName:
			UE_LOG(LogPCG, Error, TEXT("PCGActorSelector::GetFilteringFunction: Unsupported value for EPCGActorSelection - selection by name is no longer supported."));
			break;

		default:
			break;
		}

		return [](AActor* Actor) -> bool { return false; };
	}

	TArray<AActor*> FindActors(const FPCGActorSelectorSettings& Settings, const UPCGComponent* InComponent, const TFunction<bool(const AActor*)>& BoundsCheck, const TFunction<bool(const AActor*)>& SelfIgnoreCheck)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGActorSelector::FindActor);

		UWorld* World = InComponent ? InComponent->GetWorld() : nullptr;
		AActor* Self = InComponent ? InComponent->GetOwner() : nullptr;

		TArray<AActor*> FoundActors;

		if (!World)
		{
			return FoundActors;
		}

		// Early out if we have not the information necessary.
		const bool bNoTagInfo = Settings.ActorSelection == EPCGActorSelection::ByTag && Settings.ActorSelectionTag == NAME_None;
		const bool bNoClassInfo = Settings.ActorSelection == EPCGActorSelection::ByClass && !Settings.ActorSelectionClass;

		if (FilterRequired(Settings) && (bNoTagInfo || bNoClassInfo))
		{
			return FoundActors;
		}

		// We pass FoundActor ref, that will be captured by the FilteringFunction
		// It will modify the FoundActor pointer to the found actor, if found.
		TFunction<bool(AActor*)> FilteringFunction = PCGActorSelector::GetFilteringFunction(Settings, BoundsCheck, SelfIgnoreCheck, FoundActors);

		// In case of iterating over all actors in the world, call our filtering function and get out.
		if (Settings.ActorFilter == EPCGActorFilter::AllWorldActors)
		{
			// A potential optimization if we know the sought actors are collide-able could be to obtain overlaps via a collision query.
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

		case EPCGActorFilter::Original:
		{
			APCGPartitionActor* PartitionActor = Cast<APCGPartitionActor>(Self);
			UPCGComponent* OriginalComponent = (PartitionActor && InComponent) ? PartitionActor->GetOriginalComponent(InComponent) : nullptr;
			AActor* OriginalActor = OriginalComponent ? OriginalComponent->GetOwner() : nullptr;
			if (OriginalActor)
			{
				ActorsToCheck.Add(OriginalActor);
			}
			else if (Self)
			{
				ActorsToCheck.Add(Self);
			}
		}

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

	AActor* FindActor(const FPCGActorSelectorSettings& InSettings, UPCGComponent* InComponent, const TFunction<bool(const AActor*)>& BoundsCheck, const TFunction<bool(const AActor*)>& SelfIgnoreCheck)
	{
		// In order to make sure we don't try to select multiple, we'll do a copy of the settings here.
		FPCGActorSelectorSettings Settings = InSettings;
		Settings.bSelectMultiple = false;

		TArray<AActor*> Actors = FindActors(Settings, InComponent, BoundsCheck, SelfIgnoreCheck);
		return Actors.IsEmpty() ? nullptr : Actors[0];
	}
}

FPCGActorSelectionKey::FPCGActorSelectionKey(FName InTag)
{
	Selection = EPCGActorSelection::ByTag;
	Tag = InTag;
}

FPCGActorSelectionKey::FPCGActorSelectionKey(TSubclassOf<AActor> InSelectionClass)
{
	Selection = EPCGActorSelection::ByClass;
	ActorSelectionClass = InSelectionClass;
}

bool FPCGActorSelectionKey::IsMatching(const AActor* InActor) const
{
	if (!InActor)
	{
		return false;
	}

	switch (Selection)
	{
	case EPCGActorSelection::ByTag:
		return InActor->ActorHasTag(Tag);
	case EPCGActorSelection::ByClass:
		return InActor->IsA(ActorSelectionClass);
	default:
		return false;
	}
}

TArray<FPCGActorSelectionKey> FPCGActorSelectionKey::GenerateAllKeysForActor(const AActor* InActor)
{
	TArray<FPCGActorSelectionKey> Result;

	if (!InActor)
	{
		return Result;
	}

	Result.Reserve(InActor->Tags.Num() + 1);
	Result.Emplace(InActor->GetClass());
	for (FName Tag : InActor->Tags)
	{
		Result.Emplace(Tag);
	}

	return Result;
}

bool FPCGActorSelectionKey::operator==(const FPCGActorSelectionKey& InOther) const
{
	if (Selection != InOther.Selection)
	{
		return false;
	}

	switch (Selection)
	{
	case EPCGActorSelection::ByTag:
		return Tag == InOther.Tag;
	case EPCGActorSelection::ByClass:
		return ActorSelectionClass == InOther.ActorSelectionClass;
	default:
		return false;
	}
}

uint32 GetTypeHash(const FPCGActorSelectionKey& In)
{
	return HashCombine(HashCombine(GetTypeHash(In.Selection), GetTypeHash(In.Tag)), GetTypeHash(In.ActorSelectionClass));
}

#if WITH_EDITOR
FText FPCGActorSelectorSettings::GetTaskNameSuffix() const
{
	if (ActorFilter == EPCGActorFilter::AllWorldActors)
	{
		if (ActorSelection == EPCGActorSelection::ByClass)
		{
			return (ActorSelectionClass.Get() ? ActorSelectionClass->GetDisplayNameText() : FText::FromName(NAME_None));
		}
		else if (ActorSelection == EPCGActorSelection::ByTag)
		{
			return FText::FromName(ActorSelectionTag);
		}
	}
	else if(const UEnum* EnumPtr = StaticEnum<EPCGActorFilter>())
	{
		return EnumPtr->GetDisplayNameTextByValue(static_cast<__underlying_type(EPCGActorFilter)>(ActorFilter));
	}

	return FText();
}

FName FPCGActorSelectorSettings::GetTaskName(const FText& Prefix) const
{
	return FName(FText::Format(NSLOCTEXT("PCGActorSelectorSettings", "NodeTitleFormat", "{0} ({1})"), Prefix, GetTaskNameSuffix()).ToString());
}
#endif // WITH_EDITOR

FPCGActorSelectionKey FPCGActorSelectorSettings::GetAssociatedKey() const
{
	// If we don't look for AllWorldActors, it means we track the pcg component, which should be already picked up by the tracking system.
	if (ActorFilter != EPCGActorFilter::AllWorldActors)
	{
		return FPCGActorSelectionKey();
	}

	switch (ActorSelection)
	{
	case EPCGActorSelection::ByTag:
		return FPCGActorSelectionKey(ActorSelectionTag);
	case EPCGActorSelection::ByClass:
		return FPCGActorSelectionKey(ActorSelectionClass);
	default:
		return FPCGActorSelectionKey();
	}
}

FPCGActorSelectorSettings FPCGActorSelectorSettings::ReconstructFromKey(const FPCGActorSelectionKey& InKey)
{
	FPCGActorSelectorSettings Result{};
	Result.ActorFilter = EPCGActorFilter::AllWorldActors;
	Result.ActorSelection = InKey.Selection;
	Result.ActorSelectionTag = InKey.Tag;
	Result.ActorSelectionClass = InKey.ActorSelectionClass;

	return Result;
}