// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Map.h"
#include "Misc/Guid.h"
#include "Misc/Optional.h"
#include "UObject/SoftObjectPath.h"
#include "WorldPartitionActorFilter.generated.h"

USTRUCT()
struct ENGINE_API FWorldPartitionActorFilter
{
	GENERATED_USTRUCT_BODY()

	FWorldPartitionActorFilter() {}
	~FWorldPartitionActorFilter();

	FWorldPartitionActorFilter(const FWorldPartitionActorFilter& Other);
	FWorldPartitionActorFilter(FWorldPartitionActorFilter&& Other);

	FWorldPartitionActorFilter& operator=(const FWorldPartitionActorFilter& Other);
	FWorldPartitionActorFilter& operator=(FWorldPartitionActorFilter&& Other);

#if WITH_EDITORONLY_DATA
	FWorldPartitionActorFilter(const FString& InDisplayName) : DisplayName(InDisplayName) {}
	
	void AddChildFilter(const FGuid& InGuid, FWorldPartitionActorFilter* InChildFilter);
	void RemoveChildFilter(const FGuid& InGuid);
	void ClearChildFilters();
	
	void Override(const FWorldPartitionActorFilter& Other);

	const TMap<FGuid, FWorldPartitionActorFilter*>& GetChildFilters() const { return ChildFilters; }
	FWorldPartitionActorFilter* GetParentFilter() const { return Parent; }

	struct FDataLayerFilter
	{
		FDataLayerFilter() {}

		FDataLayerFilter(const FString& InDisplayName, bool bInIncluded)
			: bIncluded(bInIncluded)
			, DisplayName(InDisplayName) {}

		// True if DataLayer actors should be included
		bool bIncluded;

		// Transient
		FString DisplayName;

	};

	// Transient
	FString DisplayName;
	// List of DataLayer Assets to Include or Exclude, missing DataLayer Assets will use default behavior
	TMap<FSoftObjectPath, FDataLayerFilter> DataLayerFilters;

	friend FArchive& operator<<(FArchive& Ar, FWorldPartitionActorFilter& Filter);
	bool Serialize(FArchive& Ar);

	// Operators.
	bool operator==(const FWorldPartitionActorFilter& Other) const;

	bool operator!=(const FWorldPartitionActorFilter& Other) const
	{
		return !(*this == Other);
	}

	// Needed for Copy/Paste/ResetToDefault
	bool ExportTextItem(FString& ValueStr, FWorldPartitionActorFilter const& DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope) const;
	bool ImportTextItem(const TCHAR*& Buffer, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText);

	static void RequestFilterRefresh(bool bIsFromUserChange);

	DECLARE_MULTICAST_DELEGATE(FOnWorldPartitionActorFilterChanged);
	static FOnWorldPartitionActorFilterChanged& GetOnWorldPartitionActorFilterChanged() { return OnWorldPartitionActorFilterChanged; }
private:
	// Transient
	FWorldPartitionActorFilter* Parent = nullptr;
	// Map of FWorldPartitionActorFilters per Child Level Instance, recursive
	TMap<FGuid, FWorldPartitionActorFilter*> ChildFilters;
	// Static Event for when some filter changes
	static FOnWorldPartitionActorFilterChanged OnWorldPartitionActorFilterChanged;
#endif
};

#if WITH_EDITORONLY_DATA
template<> struct TStructOpsTypeTraits<FWorldPartitionActorFilter> : public TStructOpsTypeTraitsBase2<FWorldPartitionActorFilter>
{
	enum
	{
		WithSerializer = true,
		WithIdenticalViaEquality = true,
		WithImportTextItem = true,
		WithExportTextItem = true
	};
};
#endif

