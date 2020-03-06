// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataSourceFilter.h"
#include "DataSourceFilterSet.h"
#include "UObject/ObjectKey.h"
#include "Engine/DataAsset.h"

#include "SourceFilterCollection.generated.h"

UCLASS()
class SOURCEFILTERINGTRACE_API USourceFilterCollection : public UDataAsset
{
	GENERATED_BODY()
public:
	virtual ~USourceFilterCollection() {}

	/** Begin UDataAsset overrides */
	virtual void Serialize(FArchive& Ar) override;
	/** End UDataAsset overrides */

	/** Add Filter instance to the collection, will be added at the root level */
	void AddFilter(UDataSourceFilter* NewFilter);

	/** Add a Filter instance of the provided class, will be added at the root level */
	UDataSourceFilter* AddFilterOfClass(const TSubclassOf<UDataSourceFilter>& FilterClass);
	/** Add a Filter instance of the provided class, added to the provided filter set */
	UDataSourceFilter* AddFilterOfClassToSet(const TSubclassOf<UDataSourceFilter>& FilterClass, UDataSourceFilterSet* FilterSet);

	/** Remove Filter instance, regardless of whether it is a root-level filter or part of a filter set  */
	void RemoveFilter(UDataSourceFilter* ToRemoveFilter);
	/** Remove a Filter Instance from a specific Filter Set */
	void RemoveFilterFromSet(UDataSourceFilter* ToRemoveFilter, UDataSourceFilterSet* FilterSet);
	
	/** Replace a Filter Instance with another */
	void ReplaceFilter(UDataSourceFilter* Destination, UDataSourceFilter* Source);
	
	/** Move a Filter instance to a specific Filter Set (moved to root-level if Destination = nullptr) */
	void MoveFilter(UDataSourceFilter* Filter, UDataSourceFilterSet* Destination);
	
	/** Convert a Filter Instance to a Filter Set (with provided mode), this creates set containing the replace filter */
	UDataSourceFilterSet* ConvertFilterToSet(UDataSourceFilter* ReplacedFilter, EFilterSetMode Mode);
	/** Create a Filter set (with provided mode) containing both Filter Instances */
	UDataSourceFilterSet* MakeFilterSet(UDataSourceFilter* FilterOne, UDataSourceFilter* FilterTwo, EFilterSetMode Mode);

	/** Creates an empty Filter Set (with provided mode) */
	UDataSourceFilterSet* MakeEmptyFilterSet(EFilterSetMode Mode);

	/** Creates a new collection of filter (sets), provided the filter class names and parent/child relationship */
	void AddFiltersFromPreset(const TArray<FString>& ClassNames, const TMap<int32, int32>& ChildToParentIndices);
	
	/** Resets all contained filter data */
	void Reset();
	
	/** Returns all top-level Filter instances */
	const TArray<UDataSourceFilter*>& GetFilters() const { return Filters; }

	/** Copies Filter data from other provided Filter Collection*/
	void CopyData(USourceFilterCollection* OtherCollection);
protected:
	/** Recursively removes filter and any contained child filters  */
	void RemoveFilterRecursive(UDataSourceFilter* ToRemoveFilter);

	/** Adds unique filter class name */
	void AddClassName(UDataSourceFilter* Filter);
	void RecursiveRetrieveFilterClassNames(UDataSourceFilter* Filter);
	UDataSourceFilter* RecursiveCopyFilter(UDataSourceFilter* Filter, int32& FilterOffset);
	
	template<typename T> T* CreateNewFilter(UClass* Class = T::StaticClass());
	void DestroyFilter(UDataSourceFilter* Filter);
protected:
	/** Root-level filter instances */
	UPROPERTY(VisibleAnywhere, Category=Filtering)
	TArray<UDataSourceFilter*> Filters;

	/** Mapping from Filter Instance FObjectKeys to their class names */
	TMap<FObjectKey, FString> FilterClassMap;

	/** Flat version of the Filter classes contained by this collection, stored according to Filters ordering, with child filters inline */
	UPROPERTY()
	TArray<FString> FilterClasses;

	/** Child / Parent mapping for Filter (sets) */
	UPROPERTY()
	TMap<UDataSourceFilter*, UDataSourceFilterSet*> ChildToParent;
};