// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "LevelSnapshotFilters.h"
#include "DisjunctiveNormalFormFilter.generated.h"

class UConjunctionFilter;

/*
 * Manages logic for combining filters in the editor.
 * This filter may have no children: in this case, the filter returns true.
 *
 * Disjunctive normal form = ORs of ANDs. Example: (a && !b) || (c && d) || e
 */
UCLASS(meta = (InternalSnapshotFilter))
class UDisjunctiveNormalFormFilter : public ULevelSnapshotFilter
{
	GENERATED_BODY()
public:

	/** Creates a new AND-filter and adds it to the list of children. */
	UConjunctionFilter* CreateChild();
	/** Removes a filter previously created by CreateChild. */
	void RemoveConjunction(UConjunctionFilter* Child);
	const TArray<UConjunctionFilter*>& GetChildren() const;
	
	//~ Begin ULevelSnapshotFilter Interface
	bool IsActorValid(const FName ActorName, const UClass* ActorClass) const override;
	bool IsPropertyValid(const FName ActorName, const UClass* ActorClass, const FString& PropertyName) const override;
	//~ End ULevelSnapshotFilter Interface

	DECLARE_EVENT_TwoParams(UDisjunctiveNormalFormFilter, FOnChildrenChanged, UConjunctionFilter* /*AffectedFilter*/, int32 /*ChildIndex*/);
	FOnChildrenChanged OnChildAdded;
	FOnChildrenChanged OnChildRemoved;
	
private:

	UPROPERTY()
	TArray<UConjunctionFilter*> Children;
	
};
