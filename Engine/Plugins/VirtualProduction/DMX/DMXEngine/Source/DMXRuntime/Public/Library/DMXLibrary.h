// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Library/DMXObjectBase.h"

#include "Library/DMXEntity.h"
#include "Library/DMXInputPortReference.h"
#include "Library/DMXOutputPortReference.h"

#include "Templates/SubclassOf.h"
#include "Misc/Guid.h"

#include "DMXLibrary.generated.h"


/** Called when the list of entities is changed by either adding or removing entities */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnEntitiesUpdated, class UDMXLibrary*);

UCLASS(BlueprintType, Blueprintable, AutoExpandCategories = DMX)
class DMXRUNTIME_API UDMXLibrary
	: public UDMXObjectBase
{
	GENERATED_BODY()

public:
	// ~Begin UObject Interface
	virtual void PostLoad() override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent) override;
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

	/** Returns the property name of the input port references array */
	FName GetInputPortReferencesPropertyName() const { return GET_MEMBER_NAME_CHECKED(UDMXLibrary, InputPortReferences); }

	/** Returns the property name of the output port references array */
	FName GetOutputPortReferencesPropertyName() const { return GET_MEMBER_NAME_CHECKED(UDMXLibrary, OutputPortReferences); }

	/** Broadcast when the ports changed */
	FSimpleMulticastDelegate OnPortsChanged;

protected:
	/** Updates the ports from what's set in the Input and Output Port References arrays */
	void UpdatePorts();

	/** Input ports of the Library */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, NonTransactional, Category = "DMX", Meta = (DisplayName = "Input Ports"))
	TArray<FDMXInputPortReference> InputPortReferences;

	/** Output ports of the Library */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, NonTransactional, Category = "DMX", Meta = (DisplayName = "Output Ports"))
	TArray<FDMXOutputPortReference> OutputPortReferences;

private:
#if WITH_EDITOR
	/** Helper to return the last index of a duplicate in the port reference arrays. Only expect one duplicate since the user cannot property change many */
	template <typename PortReferenceType>
	const int32 GetLastIndexOfDuplicatePortReference(const TArray<PortReferenceType>& PortReferenceArray) const
	{
		for (int32 FirstIndex = 0; FirstIndex < PortReferenceArray.Num(); FirstIndex++)
		{
			const PortReferenceType& TestedElement = PortReferenceArray[FirstIndex];
			const int32 LastIndex = PortReferenceArray.FindLastByPredicate([TestedElement](const PortReferenceType& PortRef) {
				return &PortRef != &TestedElement;
				});
			if (FirstIndex != LastIndex)
			{
				// Either the valid last index or INDEX_NONE
				return LastIndex;
			}
		}
		return INDEX_NONE;
	}
#endif // WITH_EDITOR

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
