// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "SmartObjectTypes.h"
#include "SmartObjectCollection.generated.h"

class USmartObjectComponent;

/** Struct representing a unique registered component in the collection actor */
USTRUCT()
struct SMARTOBJECTSMODULE_API FSmartObjectCollectionEntry
{
	GENERATED_BODY()
public:
	FSmartObjectCollectionEntry() = default;
	FSmartObjectCollectionEntry(const FSmartObjectID& SmartObjectID, const USmartObjectComponent& SmartObjectComponent);

	USmartObjectComponent* GetComponent() const;
	const FSmartObjectID& GetID() const { return ID; }
	FString Describe() const;

protected:
	// Only the collection can access the path since the way we reference the component
	// might change to better support streaming so keeping this as encapsulated as possible
	friend class ASmartObjectCollection;

	UPROPERTY(VisibleAnywhere, Category = SmartObject, meta = (ShowOnlyInnerProperties))
	FSmartObjectID ID;

	UPROPERTY()
	FSoftObjectPath Path;
};

/** Actor holding smart object persistent data */
UCLASS(NotBlueprintable, hidecategories = (Rendering, Replication, Collision, Input, HLOD, Actor, LOD, Cooking, WorldPartition))
class SMARTOBJECTSMODULE_API ASmartObjectCollection : public AActor
{
	GENERATED_BODY()

public:
	TConstArrayView<FSmartObjectCollectionEntry> GetEntries() const;

protected:
	friend class USmartObjectSubsystem;

	ASmartObjectCollection(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//~ Begin UObject/AActor Interface
	virtual void PostActorCreated() override;
	virtual void PostLoad() override;
	virtual void Destroyed() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PreRegisterAllComponents() override;
	virtual void PostUnregisterAllComponents() override;

#if WITH_EDITOR
	virtual void PostEditUndo() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool SupportsExternalPackaging() const override { return false; }
	//~ End UObject/AActor Interface
	bool IsBuildOnDemand() const { return bBuildOnDemand; }
#endif // WITH_EDITOR

	bool IsRegistered() const { return bRegistered; }
	void OnRegistered();
	void OnUnregistered();

	FSmartObjectID AddSmartObject(USmartObjectComponent& SOComponent);
	void RemoveSmartObject(USmartObjectComponent& SOComponent);
	USmartObjectComponent* GetSmartObjectComponent(const FSmartObjectID& SmartObjectID) const;

protected:

	bool RegisterWithSubsystem(const FString Context);
	bool UnregisterWithSubsystem(const FString Context);

	UPROPERTY(VisibleAnywhere, Category = SmartObject)
	TArray<FSmartObjectCollectionEntry> CollectionEntries;

	TMap<FSmartObjectID, FSoftObjectPath> RegisteredIdToObjectMap;

	bool bRegistered = false;

#if WITH_EDITOR
	UFUNCTION(CallInEditor, Category = SmartObject)
	void RebuildCollection();
	void RebuildCollection(TConstArrayView<USmartObjectComponent*> Components);
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = SmartObject)
	bool bBuildOnDemand = false;
#endif // WITH_EDITORONLY_DATA
};
