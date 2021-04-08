// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LevelSnapshotFilters.h"
#include "EditorFilter.generated.h"

class UEditorFilter;

UENUM()
enum class EEditorFilterBehavior : uint8
{
	/* Pass on same result */
	DoNotNegate,
    /* Invert the result */ 
    Negate,
    /* Ignore the result */
    Ignore,
    /* The mixed result, currently using for a group of filters */
    // UMETA is a hack so Mixed is not an option that user can choose in Details panel. Proper way to fix it is by removing Mixed from the enum and handling the case separately in UEditorFilter.
    Mixed  UMETA(Hidden) 
};

UCLASS(meta = (InternalSnapshotFilter))
class UEditorFilter : public ULevelSnapshotFilter
{
	GENERATED_BODY()
public:
	//~ Begin UObject interface
	virtual void PostLoad() override;
	//~ End UObject interface

	/** Get current filter state */
	virtual EEditorFilterBehavior GetEditorFilterBehavior() const { return EditorFilterBehavior; }

	/** Get all filter children, should be overridden in the inherited classes */
	virtual TArray<UEditorFilter*> GetEditorChildren() { return TArray<UEditorFilter*>(); }

	/**  Set the filter behavior, it can optionally affect children */
	virtual void SetEditorFilterBehavior(const EEditorFilterBehavior InFilterBehavior, const bool bIncludeChildren = false);

	/**  Move filter state to next filter state, it can optionally affect children */
	virtual void IncrementEditorFilterBehavior(const bool bIncludeChildren = false);

	/**  Updates state for all children, it can optionally affect grand children */
	virtual void UpdateAllChildrenEditorFilterBehavior(EEditorFilterBehavior InFilterBehavior, const bool bIncludeGrandChildren = false);

	/** Updates filter state this filter from the first child */
	virtual void SetEditorFilterBehaviorFromChild();

	/** Updates filter state this filter by given child filter state and optional children filter array */
	virtual void SetEditorFilterBehaviorFromChild(const EEditorFilterBehavior InFilterBehavior, TArray<UEditorFilter*> InChildren = TArray<UEditorFilter*>());

	/** Updates the parent filter state from the children states */
	virtual void UpdateParentFilterFromChild(const bool bRecursively = false);

	/** Set the parent filter object */
	virtual void SetParentFilter(UEditorFilter* InEditorFilter);

	UEditorFilter* GetParentFilter() const { return ParentFilter; }

protected:
	
	/* Whether to pass on the result of the filter, negate it, or ignore it. */
	UPROPERTY(EditAnywhere, Category = "Filter")
	EEditorFilterBehavior EditorFilterBehavior = EEditorFilterBehavior::DoNotNegate;

	/** Parent Editor filter */
	UPROPERTY();
	UEditorFilter* ParentFilter;
};
