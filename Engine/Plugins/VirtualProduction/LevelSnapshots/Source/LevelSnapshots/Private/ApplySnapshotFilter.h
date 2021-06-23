// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PropertySelectionMap.h"

class AActor;
class UActorComponent;
class ULevelSnapshot;
class ULevelSnapshotFilter;

class FApplySnapshotFilter
{
public:

	static FApplySnapshotFilter Make(ULevelSnapshot* Snapshot, AActor* DeserializedSnapshotActor, AActor* WorldActor, const ULevelSnapshotFilter* Filter);
	
	FApplySnapshotFilter& AllowUnchangedProperties(bool bNewValue)
	{
		bAllowUnchangedProperties = bNewValue;
		return *this;
	}
	FApplySnapshotFilter& AllowNonEditableProperties(bool bNewValue)
	{
		bAllowNonEditableProperties = bNewValue;
		return *this;
	}
	
	void ApplyFilterToFindSelectedProperties(FPropertySelectionMap& MapToAddTo);

private:

	enum class EFilterObjectPropertiesResult
	{
		HasCustomSubobjects,
		HasOnlyNormalProperties
	};
	
	struct FPropertyContainerContext
	{
		FPropertySelection& SelectionToAddTo;
	
		UStruct* ContainerClass;
		void* SnapshotContainer;
		void* WorldContainer;
			
		/* Information passed to blueprints. Property name is appended to this.
		 * Example: [FooComponent] [BarStructPropertyName]...
		 */
		TArray<FString> AuthoredPathInformation;

		/* Keeps track of the structs leading to this container */
		FLevelSnapshotPropertyChain PropertyChain;
		/* Class that PropertyChain begins from. */
		UClass* RootClass;

		FPropertyContainerContext(FPropertySelection& SelectionToAddTo, UStruct* ContainerClass, void* SnapshotContainer, void* WorldContainer, const TArray<FString>& AuthoredPathInformation, const FLevelSnapshotPropertyChain& PropertyChain, UClass* RootClass);
	};
	
	FApplySnapshotFilter(ULevelSnapshot* Snapshot, AActor* DeserializedSnapshotActor, AActor* WorldActor, const ULevelSnapshotFilter* Filter);
	bool EnsureParametersAreValid() const;
	
	void AnalyseComponentProperties(FPropertySelectionMap& MapToAddTo);

	void FilterActorPair(FPropertySelectionMap& MapToAddTo);
	/* @return Whether any property selection was added */
	bool FilterSubobjectPair(FPropertySelectionMap& MapToAddTo, UObject* SnapshotSubobject, UObject* WorldSubobject);
	void FilterStructPair(FPropertyContainerContext& Parent, FStructProperty* StructProperty);
	EFilterObjectPropertiesResult FindAndFilterCustomSubobjectPairs(FPropertySelectionMap& MapToAddTo, UObject* SnapshotOwner, UObject* WorldOwner);

	void AnalyseRootProperties(FPropertyContainerContext& ContainerContext, UObject* SnapshotObject, UObject* WorldObject);
	void AnalyseStructProperties(FPropertyContainerContext& ContainerContext);
	void HandleStructProperties(FPropertyContainerContext& ContainerContext, FProperty* PropertyToHandle);
	
	enum ECheckSubproperties
	{
		CheckSubproperties,
        SkipSubproperties
    }; 
	ECheckSubproperties AnalyseProperty(FPropertyContainerContext& ContainerContext, FProperty* PropertyInCommon, bool bSkipEqualityTest = false);

	
	ULevelSnapshot* Snapshot;
	AActor* DeserializedSnapshotActor;
	AActor* WorldActor;
	const ULevelSnapshotFilter* Filter;

	/* Do we allow properties that do not show up in the details panel? */
	bool bAllowNonEditableProperties = false;

	/* Do we allow adding properties that did not change to the selection map? */
	bool bAllowUnchangedProperties = false;
	
};
