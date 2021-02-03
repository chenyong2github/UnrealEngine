// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LevelSnapshotFilters.h"
#include "NegatableFilter.generated.h"

/*
 * Calls a child filter and possibly negates its results.
 */
UCLASS(meta = (InternalSnapshotFilter))
class UNegatableFilter : public ULevelSnapshotFilter
{
	GENERATED_BODY()
public:

	/* Wraps the given filter with a negation. Defaults to ChildFilter's outer. */
	static UNegatableFilter* CreateNegatableFilter(ULevelSnapshotFilter* ChildFilter, const TOptional<UObject*>& Outer = TOptional<UObject*>());
	
	void SetShouldNegate(bool bNewShouldNegate);
	bool ShouldNegate() const;
	ULevelSnapshotFilter* GetChildFilter() const;
	FText GetDisplayName() const;

	//~ Begin ULevelSnapshotFilter Interface
	bool IsActorValid(const FName ActorName, const UClass* ActorClass) const override;
	bool IsPropertyValid(const FName ActorName, const UClass* ActorClass, const FString& PropertyName) const override;
	//~ End ULevelSnapshotFilter Interface
	
private:

	UPROPERTY()
	bool bShouldNegate = false;

	UPROPERTY()
	ULevelSnapshotFilter* ChildFilter;
	
};