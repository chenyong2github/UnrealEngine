// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EditorFilter.h"
#include "DisjunctiveNormalFormFilter.generated.h"

class UConjunctionFilter;

/*
 * Manages logic for combining filters in the editor.
 * This filter may have no children: in this case, the filter returns true.
 *
 * Disjunctive normal form = ORs of ANDs. Example: (a && !b) || (c && d) || e
 */
UCLASS(meta = (InternalSnapshotFilter))
class UDisjunctiveNormalFormFilter : public UEditorFilter
{
	GENERATED_BODY()
public:

	/** Creates a new AND-filter and adds it to the list of children. */
	UConjunctionFilter* CreateChild();
	/** Removes a filter previously created by CreateChild. */
	void RemoveConjunction(UConjunctionFilter* Child);
	const TArray<UConjunctionFilter*>& GetChildren() const;

	//~ Begin ULevelSnapshotFilter Interface
	EFilterResult::Type IsActorValid(const FIsActorValidParams& Params) const override;
	EFilterResult::Type IsPropertyValid(const FIsPropertyValidParams& Params) const override;
	//~ End ULevelSnapshotFilter Interface

	//~ Begin UEditorFilter Interface
	virtual TArray<UEditorFilter*> GetEditorChildren();
	//~ Begin UEditorFilter Interface

	DECLARE_EVENT_TwoParams(UDisjunctiveNormalFormFilter, FOnChildrenChanged, UConjunctionFilter* /*AffectedFilter*/, int32 /*ChildIndex*/);
	FOnChildrenChanged OnChildAdded;
	FOnChildrenChanged OnChildRemoved;
	
private:

	UPROPERTY()
	TArray<UConjunctionFilter*> Children;
};
