// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorFilter.h"
#include "NegatableFilter.generated.h"

/*
 * Calls a child filter and possibly negates its results.
 */
UCLASS(meta = (InternalSnapshotFilter))
class UNegatableFilter : public UEditorFilter
{
	GENERATED_BODY()
public:

	/* Wraps the given filter with a negation. Defaults to ChildFilter's outer. */
	static UNegatableFilter* CreateNegatableFilter(ULevelSnapshotFilter* ChildFilter, const TOptional<UObject*>& Outer = TOptional<UObject*>());

	ULevelSnapshotFilter* GetChildFilter() const;
	FText GetDisplayName() const;

	//~ Begin ULevelSnapshotFilter Interface
	virtual EFilterResult::Type IsActorValid(const FIsActorValidParams& Params) const override;
	virtual EFilterResult::Type IsPropertyValid(const FIsPropertyValidParams& Params) const override;
	virtual EFilterResult::Type IsDeletedActorValid(const FIsDeletedActorValidParams& Params) const override;
	virtual EFilterResult::Type IsAddedActorValid(const FIsAddedActorValidParams& Params) const override;
	//~ End ULevelSnapshotFilter Interface

	
public: // Only public to use GET_MEMBER_NAME_CHECKED with compiler checks - do not use directly.
	
	/* Display name in editor. Defaults to class name if left empty. */
	UPROPERTY(EditAnywhere, Category = "Filter")
	FString Name;

private:
	
	UPROPERTY()
	ULevelSnapshotFilter* ChildFilter;
};