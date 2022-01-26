// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"
#include "InstancedStruct.h"
#include "MassEntityTypes.h"
#include "Engine/DataAsset.h"
#include "SmartObjectDefinition.generated.h"

class UGameplayBehaviorConfig;

/**
 * Abstract class that can be extended to bind a new type of behavior framework
 * to the smart objects by defining the required definition.
 */
UCLASS(Abstract, NotBlueprintable, EditInlineNew, CollapseCategories, HideDropdown)
class SMARTOBJECTSMODULE_API USmartObjectBehaviorDefinition : public UObject
{
	GENERATED_BODY()
};

/**
 * SmartObject behavior definition for the GameplayBehavior framework
 */
UCLASS()
class SMARTOBJECTSMODULE_API USmartObjectGameplayBehaviorDefinition : public USmartObjectBehaviorDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category = SmartObject, Instanced)
	UGameplayBehaviorConfig* GameplayBehaviorConfig;
};

/**
 * Persistent and sharable definition of a smart object slot.
 */
USTRUCT()
struct SMARTOBJECTSMODULE_API FSmartObjectSlotDefinition
{
	GENERATED_BODY()

	/** This slot is available only for users matching this query. */
	UPROPERTY(EditDefaultsOnly, Category = SmartObject)
	FGameplayTagQuery UserTagFilter;

	/** Offset relative to the parent object where the slot is located. */
	UPROPERTY(EditDefaultsOnly, Category = SmartObject)
	FVector Offset = FVector::ZeroVector;

	/** Rotation relative to the parent object. */
	UPROPERTY(EditDefaultsOnly, Category = SmartObject)
	FRotator Rotation = FRotator::ZeroRotator;

	/** Custom data that can be added to the slot definition and access through a FSmartObjectSlotView */
	UPROPERTY(EditDefaultsOnly, Category = "SmartObject", meta = (BaseStruct = "SmartObjectSlotDefinitionData", ExcludeBaseStruct))
	TArray<FInstancedStruct> Data;

	/**
	 * All available definitions associated to this slot.
	 * This allows multiple frameworks to provide their specific behavior definition to the slot.
	 * Note that there should be only one definition of each type since the first one will be selected.
	 */
	UPROPERTY(EditDefaultsOnly, Category = SmartObject, Instanced)
	TArray<USmartObjectBehaviorDefinition*> BehaviorDefinitions;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = SmartObject)
	FColor DEBUG_DrawColor;
#endif // WITH_EDITORONLY_DATA

	FSmartObjectSlotDefinition()
		: Offset(FVector::ZeroVector), Rotation(FRotator::ZeroRotator)
#if WITH_EDITORONLY_DATA
		, DEBUG_DrawColor(FColor::Yellow)
#endif // WITH_EDITORONLY_DATA
	{}
};

/**
 * Helper struct to wrap basic functionalities to store the index of a slot in a SmartObject definition
 */
USTRUCT(BlueprintType)
struct SMARTOBJECTSMODULE_API FSmartObjectSlotIndex
{
	GENERATED_BODY()

	explicit FSmartObjectSlotIndex(const int32 InSlotIndex = INDEX_NONE) : Index(InSlotIndex) {}

	bool IsValid() const { return Index != INDEX_NONE; }
	void Invalidate() { Index = INDEX_NONE; }

	operator int32() const { return Index; }

	bool operator==(const FSmartObjectSlotIndex& Other) const { return Index == Other.Index; }
	friend FString LexToString(const FSmartObjectSlotIndex& SlotIndex) { return FString::Printf(TEXT("[Slot:%d]"), SlotIndex.Index); }

private:
	UPROPERTY(Transient)
	int32 Index = INDEX_NONE;
};

/**
 * SmartObject definition asset. Contains sharable information that can be used by multiple SmartObject instances at runtime.
 */
UCLASS(BlueprintType, Blueprintable, CollapseCategories)
class SMARTOBJECTSMODULE_API USmartObjectDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Retrieves a specific type of behavior definition for a given slot.
	 * When the slot doesn't provide one or if the provided index is not valid
	 * the search will look in the object default definitions.
	 *
	 * @param SlotIndex			Index of the slot for which the definition is requested
	 * @param DefinitionClass	Type of the requested behavior definition
	 * @return The behavior definition found or null if none are available for the requested type.
	 */
	const USmartObjectBehaviorDefinition* GetBehaviorDefinition(const FSmartObjectSlotIndex& SlotIndex, const TSubclassOf<USmartObjectBehaviorDefinition>& DefinitionClass) const;

	/** Returns a view on all the slot definitions */
	TConstArrayView<FSmartObjectSlotDefinition> GetSlots() const { return Slots; }

#if WITH_EDITOR
	/** Returns a view on all the slot definitions */
	TArrayView<FSmartObjectSlotDefinition> GetMutableSlots() { return Slots; }
#endif

	/** Return bounds encapsulating all slots */
	FBox GetBounds() const;

	/** Adds and returns a reference to a defaulted slot (used for testing purposes) */
	FSmartObjectSlotDefinition& DebugAddSlot() { return Slots.AddDefaulted_GetRef(); }

	/**
	 * Returns the transform (in world space) of the given slot index.
	 * @param OwnerTransform Transform (in world space) of the slot owner.
	 * @param SlotIndex Index within the list of slots.
	 * @return Transform (in world space) of the slot associated to SlotIndex.
	 * @note Method will ensure on invalid invalid index.
	 */
	TOptional<FTransform> GetSlotTransform(const FTransform& OwnerTransform, const FSmartObjectSlotIndex SlotIndex) const;

	/** Returns the tag query to run on the user tags to accept this definition */
	const FGameplayTagQuery& GetUserTagFilter() const { return UserTagFilter; }

	/** Returns the tag query to run on the owner tags to accept this definition */
	const FGameplayTagQuery& GetObjectTagFilter() const { return ObjectTagFilter; }

	/** Returns the list of tags describing the activity associated to this definition */
	const FGameplayTagContainer& GetActivityTags() const { return ActivityTags; }

	/**
	 *	Performs validation and logs errors if any. An object using an invalid definition
	 *	will not be registered in the simulation.
	 *	The result of the validation is stored until next validation and can be retrieved using `IsValid`.
	 *	@return true if the definition is valid
	 */
	bool Validate() const;

	/** Provides a description of the definition */
	friend FString LexToString(const USmartObjectDefinition& Definition)
	{
		return FString::Printf(TEXT("NumSlots=%d NumDefs=%d HasUserFilter=%s HasObjectFilter=%s"),
			Definition.Slots.Num(),
			Definition.DefaultBehaviorDefinitions.Num(),
			*LexToString(!Definition.UserTagFilter.IsEmpty()),
			*LexToString(!Definition.ObjectTagFilter.IsEmpty()));
	}

	/** Returns result of the last validation if `Validate` was called; unset otherwise. */
	TOptional<bool> IsValid() const { return bValid; }

#if WITH_EDITORONLY_DATA
	/** Actor class used for previewing the definition in the asset editor. */
	UPROPERTY(EditDefaultsOnly, Category = SmartObject)
	TSoftClassPtr<AActor> PreviewClass;

	/** Path of the static mesh used for previewing the definition in the asset editor. */
	UPROPERTY(EditDefaultsOnly, Category = SmartObject)
	FSoftObjectPath PreviewMeshPath;
#endif

private:
	/** Finds first behavior definition of a given class in the provided list of definitions. */
	static const USmartObjectBehaviorDefinition* GetBehaviorDefinitionByType(const TArray<USmartObjectBehaviorDefinition*>& BehaviorDefinitions, const TSubclassOf<USmartObjectBehaviorDefinition>& DefinitionClass);

	/**
	 * Where SmartObject's user needs to stay to be able to activate it. These
	 * will be used by AI to approach the object. Locations are relative to object's location.
	 */
	UPROPERTY(EditDefaultsOnly, Category = SmartObject)
	TArray<FSmartObjectSlotDefinition> Slots;

	/** List of behavior definitions of different types provided to SO's user if the slot does not provide one. */
	UPROPERTY(EditDefaultsOnly, Category = SmartObject, Instanced)
	TArray<USmartObjectBehaviorDefinition*> DefaultBehaviorDefinitions;

	/** This object is available only for users matching this query. */
	UPROPERTY(EditDefaultsOnly, Category = SmartObject)
	FGameplayTagQuery UserTagFilter;

	/** This object is available only when instance matches this query. */
	UPROPERTY(EditDefaultsOnly, Category = SmartObject)
	FGameplayTagQuery ObjectTagFilter;

	/** Tags identifying this Smart Object's use case. Can be used while looking for objects supporting given activity */
	UPROPERTY(EditDefaultsOnly, Category = SmartObject)
	FGameplayTagContainer ActivityTags;

	mutable TOptional<bool> bValid;
};

/**
 * Mass Fragment used to share slot definition between all slot instances using that definition.
 */
USTRUCT()
struct SMARTOBJECTSMODULE_API FSmartObjectSlotDefinitionFragment : public FMassSharedFragment
{
	GENERATED_BODY()

	FSmartObjectSlotDefinitionFragment() = default;
	explicit FSmartObjectSlotDefinitionFragment(const USmartObjectDefinition& InObjectDefinition, const FSmartObjectSlotDefinition& InSlotDefinition)
		: SmartObjectDefinition(&InObjectDefinition), SlotDefinition(&InSlotDefinition) {}

	/** Pointer to the parent object definition to preserve slot definition pointer validity. */
	UPROPERTY(Transient)
	const USmartObjectDefinition* SmartObjectDefinition = nullptr;

	/** Pointer to the slot definition contained by the SmartObject definition. */
	const FSmartObjectSlotDefinition* SlotDefinition = nullptr;
};
