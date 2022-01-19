// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "SmartObjectTypes.h"
#include "SmartObjectCollection.generated.h"

class USmartObjectDefinition;
class USmartObjectComponent;

/** Struct representing a unique registered component in the collection actor */
USTRUCT()
struct SMARTOBJECTSMODULE_API FSmartObjectCollectionEntry
{
	GENERATED_BODY()
public:
	FSmartObjectCollectionEntry() = default;
	explicit FSmartObjectCollectionEntry(const FSmartObjectHandle& SmartObjectHandle, const USmartObjectComponent& SmartObjectComponent, const uint32 DefinitionIndex);

	const FSmartObjectHandle& GetHandle() const { return Handle; }
	const FSoftObjectPath& GetPath() const	{ return Path; }
	USmartObjectComponent* GetComponent() const;
	FTransform GetTransform() const { return Transform; }
	const FBox& GetBounds() const { return Bounds; }
	uint32 GetDefinitionIndex() const { return DefinitionIdx; }

	friend FString LexToString(const FSmartObjectCollectionEntry& CollectionEntry)
	{
		return FString::Printf(TEXT("%s - %s"), *CollectionEntry.Path.ToString(), *LexToString(CollectionEntry.Handle));
	}

protected:
	// Only the collection can access the path since the way we reference the component
	// might change to better support streaming so keeping this as encapsulated as possible
	friend class ASmartObjectCollection;

	UPROPERTY(VisibleAnywhere, Category = SmartObject, meta = (ShowOnlyInnerProperties))
	FSmartObjectHandle Handle;

	UPROPERTY()
	FSoftObjectPath Path;

	UPROPERTY()
	FTransform Transform;

	UPROPERTY()
	FBox Bounds = FBox(ForceInitToZero);

	UPROPERTY(VisibleAnywhere, Category = SmartObject)
	uint32 DefinitionIdx = INDEX_NONE;
};

/** Actor holding smart object persistent data */
UCLASS(NotBlueprintable, hidecategories = (Rendering, Replication, Collision, Input, HLOD, Actor, LOD, Cooking, WorldPartition), notplaceable)
class SMARTOBJECTSMODULE_API ASmartObjectCollection : public AActor
{
	GENERATED_BODY()

public:
	const TArray<FSmartObjectCollectionEntry>& GetEntries() const { return CollectionEntries; }
	const FBox& GetBounds() const { return Bounds;	}
	const USmartObjectDefinition* GetDefinitionForEntry(const FSmartObjectCollectionEntry& Entry) const;

#if WITH_EDITOR
	bool IsBuildingForWorldPartition() const { return bBuildingForWorldPartition;	}
	void SetBuildingForWorldPartition(const bool bValue) { bBuildingForWorldPartition = bValue;	}
	void ResetCollection(const int32 ExpectedNumElements = 0);
#endif

	void ValidateDefinitions();

protected:
	friend class USmartObjectSubsystem;

	explicit ASmartObjectCollection(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void PostActorCreated() override;
	virtual void Destroyed() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PreRegisterAllComponents() override;
	virtual void PostUnregisterAllComponents() override;

#if WITH_EDITOR
	virtual void PostEditUndo() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool SupportsExternalPackaging() const override { return false; }
	bool IsBuildOnDemand() const { return bBuildOnDemand; }

	UFUNCTION(CallInEditor, Category = SmartObject)
	void RebuildCollection();
	void RebuildCollection(const TConstArrayView<USmartObjectComponent*> Components);
	void SetBounds(const FBox InBounds) { Bounds = InBounds; }
#endif // WITH_EDITOR

	bool RegisterWithSubsystem(const FString& Context);
	bool UnregisterWithSubsystem(const FString& Context);

	void OnRegistered();
	bool IsRegistered() const { return bRegistered; }
	void OnUnregistered();

	bool AddSmartObject(USmartObjectComponent& SOComponent);
	bool RemoveSmartObject(USmartObjectComponent& SOComponent);
	USmartObjectComponent* GetSmartObjectComponent(const FSmartObjectHandle& SmartObjectHandle) const;

	UPROPERTY(VisibleAnywhere, Category = SmartObject)
	FBox Bounds = FBox(ForceInitToZero);

	UPROPERTY(VisibleAnywhere, Category = SmartObject)
	TArray<FSmartObjectCollectionEntry> CollectionEntries;

	UPROPERTY()
	TMap<FSmartObjectHandle, FSoftObjectPath> RegisteredIdToObjectMap;

	UPROPERTY(VisibleAnywhere, Category = SmartObject)
	TArray<TObjectPtr<const USmartObjectDefinition>> Definitions;

	bool bRegistered = false;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = SmartObject)
	bool bBuildOnDemand = false;

	bool bBuildingForWorldPartition = false;
#endif // WITH_EDITORONLY_DATA
};
