// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EditorFilter.h"
#include "ConjunctionFilter.generated.h"

class UNegatableFilter;

/*
 * Returns the result of and-ing all child filters.
 * It is valid to have no children: in this case, this filter return false.
 */
UCLASS(meta = (InternalSnapshotFilter))
class UConjunctionFilter : public UEditorFilter
{
	GENERATED_BODY()
public:

	/** Creates a new instance of FilterClass and places it in a new negatable filter.
	 * The resulting negatable filter is added as child.
	 */
	UNegatableFilter* CreateChild(const TSubclassOf<ULevelSnapshotFilter>& FilterClass);
	/** Removes filter created by CreateChild. */
	void RemoveChild(UNegatableFilter* Child);
	const TArray<UNegatableFilter*>& GetChildren() const;
	
	//~ Begin ULevelSnapshotFilter Interface
	virtual EFilterResult::Type IsActorValid(const FIsActorValidParams& Params) const override;
	virtual EFilterResult::Type IsPropertyValid(const FIsPropertyValidParams& Params) const override;
	virtual EFilterResult::Type IsDeletedActorValid(const FIsDeletedActorValidParams& Params) const override;
	virtual EFilterResult::Type IsAddedActorValid(const FIsAddedActorValidParams& Params) const override;
	//~ End ULevelSnapshotFilter Interface
	
	//~ Begin UEditorFilter Interface
	virtual TArray<UEditorFilter*> GetEditorChildren();
	virtual void IncrementEditorFilterBehavior(const bool bIncludeChildren = false) override;
	virtual void SetEditorFilterBehavior(const EEditorFilterBehavior InFilterBehavior, const bool bIncludeChildren = false);
	//~ Begin UEditorFilter Interface

	DECLARE_EVENT_OneParam(UConjunctionFilter, FOnChildModified, UNegatableFilter*);
	FOnChildModified OnChildAdded;
	FOnChildModified OnChildRemoved;
	
private:

	UPROPERTY()
	TArray<UNegatableFilter*> Children;
};
