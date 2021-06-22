// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Library/DMXObjectBase.h"

#include "IO/DMXInputPortReference.h"
#include "IO/DMXOutputPortReference.h"
#include "Library/DMXEntity.h"

#include "Templates/SubclassOf.h"
#include "Misc/Guid.h"

#include "DMXLibrary.generated.h"


/** Custom struct of in put and output port references for custom details customization with an enabled state */
USTRUCT(BlueprintType)
struct DMXRUNTIME_API FDMXLibraryPortReferences
{
	GENERATED_BODY()

public:
	/** Map of input port references of a Library */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, NonTransactional, Category = "DMX", Meta = (DisplayName = "Input Ports"))
	TArray<FDMXInputPortReference> InputPortReferences;

	/** Output ports of the Library of a Library */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, NonTransactional, Category = "DMX", Meta = (DisplayName = "Output Ports"))
	TArray<FDMXOutputPortReference> OutputPortReferences;
};

/** Called when the list of entities is changed by either adding or removing entities */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnEntitiesUpdated, class UDMXLibrary*);

UCLASS(BlueprintType, Blueprintable, AutoExpandCategories = DMX)
class DMXRUNTIME_API UDMXLibrary
	: public UDMXObjectBase
{
	GENERATED_BODY()

protected:
	// ~Begin UObject Interface
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	// ~End UObject Interface

public:
	/** Creates a new Entity or return an existing one with the passed in name */
	UDMXEntity* GetOrCreateEntityObject(const FString& InName, TSubclassOf<UDMXEntity> DMXEntityClass);

	/**
	 * Returns an Entity named InSearchName. If none exists, return nullptr
	 * This is not very reliable since Entities of different types can have the same name.
	 */
	UDMXEntity* FindEntity(const FString& InSearchName) const;

	/**
	 * Returns an Entity with the passed in ID.
	 * The most reliable method since Entities ID are always unique.
	 */
	UDMXEntity* FindEntity(const FGuid& Id);

	/**
	 * The finds the index of an existing entity.
	 * @return The index of the entity or INDEX_NONE if not found or the Entity doesn't belong to this Library.
	 */
	int32 FindEntityIndex(UDMXEntity* InEntity) const;

	/** Adds an existing Entity, likely created from a copy/paste operation. */
	void AddEntity(UDMXEntity* InEntity);

	/** Move an Entity to a specific index. */
	void SetEntityIndex(UDMXEntity* InEntity, const int32 NewIndex);

	/** Removes an Entity from this DMX Library searching it by name. */
	void RemoveEntity(const FString& EntityName);
	
	/** Removes an Entity from this DMX Library. */
	void FORCEINLINE RemoveEntity(UDMXEntity* InEntity)
	{
		InEntity->SetParentLibrary(nullptr);
		Entities.Remove(InEntity);
		OnEntitiesUpdated.Broadcast(this);
	}

	/** Empties this DMX Library array of Entities */
	void RemoveAllEntities();

	/** Returns all Entities in this DMX Library */
	const TArray<UDMXEntity*>& GetEntities() const;

	/** Get an array with entities from the specified UClass, but not typecast. */
	TArray<UDMXEntity*> GetEntitiesOfType(TSubclassOf<UDMXEntity> InEntityClass) const;

	/** Get an array of Entities from the specified template type, already cast. */
	template <typename EntityType>
	TArray<EntityType*> GetEntitiesTypeCast() const
	{
		TArray<EntityType*> TypeCastEntities;
		for (UDMXEntity* Entity : Entities)
		{
			if (EntityType* EntityCast = Cast<EntityType>(Entity))
			{
				TypeCastEntities.Add(EntityCast);
			}
		}
		return TypeCastEntities;
	}

	/**
	 * Calls Predicate on all Entities of the template type.
	 * This is the version without break.
	 */
	template <typename EntityType>
	void ForEachEntityOfType(TFunction<void(EntityType*)> Predicate) const
	{
		for (UDMXEntity* Entity : Entities)
		{
			if (EntityType* EntityCast = Cast<EntityType>(Entity))
			{
				Predicate(EntityCast);
			}
		}
	}

	/**
	 * Calls Predicate on all Entities of the template type
	 * Return false from the predicate to break the iteration loop or true to keep iterating.
	 */
	template <typename EntityType>
	void ForEachEntityOfTypeWithBreak(TFunction<bool(EntityType*)> Predicate) const
	{
		for (UDMXEntity* Entity : Entities)
		{
			if (EntityType* EntityCast = Cast<EntityType>(Entity))
			{
				if (!Predicate(EntityCast)) 
				{ 
					break; 
				}
			}
		}
	}

	/**
	 * Calls Predicate on all Entities of the passed in type.
	 * This is the version without break.
	 * Casting is left to the caller.
	 */
	void ForEachEntityOfType(TSubclassOf<UDMXEntity> InEntityClass, TFunction<void(UDMXEntity*)> Predicate) const;

	/**
	 * Calls Predicate on all Entities of the passed in type.
	 * Return false from the predicate to break the iteration loop or true to keep iterating.
	 * Casting is left to the caller.
	 */
	void ForEachEntityOfTypeWithBreak(TSubclassOf<UDMXEntity> InEntityClass, TFunction<bool(UDMXEntity*)> Predicate) const;

	/** Called when the list of entities is changed by either adding or removing entities */
	FOnEntitiesUpdated& GetOnEntitiesUpdated();

public:
	/** Returns all local Universe IDs in Ports */
	TSet<int32> GetAllLocalUniversesIDsInPorts() const;

	/** Returns the input ports */
	const TSet<FDMXInputPortSharedRef>& GetInputPorts() const { return InputPorts; }

	/** Returns the output ports */
	const TSet<FDMXOutputPortSharedRef>& GetOutputPorts() const { return OutputPorts; }

	/** Returns all ports as a set, slower than GetInputPorts and GetOutputPorts. */
	TSet<FDMXPortSharedRef> GenerateAllPortsSet() const;

	/** Updates the ports from what's set in the Input and Output Port References arrays */
	void UpdatePorts();

#if WITH_EDITOR
	/** Returns the name of the Ports property. */
	static FName GetPortReferencesPropertyName() { return GET_MEMBER_NAME_CHECKED(UDMXLibrary, PortReferences); }

	/** Returns the name of the Entities property. */
	static FName GetEntitiesPropertyName() { return GET_MEMBER_NAME_CHECKED(UDMXLibrary, Entities); }
#endif // WITH_EDITOR

protected:
#if WITH_EDITOR
	/** 
	 * Upgrades libraries that use controllers (before 4.27) to use ports instead (from 4.27 on). 
	 * Creates corresponding ports if they do not exist yet. 
	 */
	void UpgradeFromControllersToPorts();
#endif // WITH_EDITOR

	/** Input ports of the Library */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, NonTransactional, Category = "DMX", Meta = (ShowOnlyInnerProperties))
	FDMXLibraryPortReferences PortReferences;

private:
	/** All Fixture Types and Fixture Patches in the library */
	UPROPERTY()
	TArray<UDMXEntity*> Entities;

	/** The input ports available to the library, according to the InputPortReferences array */
	TSet<FDMXInputPortSharedRef> InputPorts;

	/** The output ports available to the library, according to the OutputPortReferences array */
	TSet<FDMXOutputPortSharedRef> OutputPorts;

	/** Called when the list of entities is changed by either adding or removing entities */
	FOnEntitiesUpdated OnEntitiesUpdated;
};
