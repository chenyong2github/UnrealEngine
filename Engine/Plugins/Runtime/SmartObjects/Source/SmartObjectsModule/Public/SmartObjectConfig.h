// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTagContainer.h"
#include "SmartObjectConfig.generated.h"

class UGameplayBehaviorConfig;

/**
 * Abstract class that can be extended to bind a new type of behavior framework
 * to the smart objects by defining the required configuration.
 */
UCLASS(Abstract, NotBlueprintable, EditInlineNew, CollapseCategories, HideDropdown)
class SMARTOBJECTSMODULE_API USmartObjectBehaviorConfigBase : public UObject
{
	GENERATED_BODY()
};

/**
 * SmartObject behavior configuration for the GameplayBehavior framework
 */
UCLASS()
class SMARTOBJECTSMODULE_API USmartObjectGameplayBehaviorConfig : public USmartObjectBehaviorConfigBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category = SmartObject, Instanced)
	UGameplayBehaviorConfig* GameplayBehaviorConfig;
};

/**
 * Persistent and sharable configuration of a smart object slot.
 */
USTRUCT()
struct SMARTOBJECTSMODULE_API FSmartObjectSlot
{
	GENERATED_BODY()

	/** This slot is available only for users matching this query. */
	UPROPERTY(EditDefaultsOnly, Category = SmartObject)
	FGameplayTagQuery UserTagFilter;

	/** Offset relative to the parent object where the slot is located. */
	UPROPERTY(EditDefaultsOnly, Category = SmartObject)
	FVector Offset;

	/** Direction relative to the parent object that the slot is facing. */
	UPROPERTY(EditDefaultsOnly, Category = SmartObject)
	FVector Direction;

	/**
	 * All available configurations associated to this slot.
	 * This allows multiple frameworks to provide their specific behavior configuration to the slot.
	 * Note that there should be only one configuration of each type since the first one will be selected.
	 */
	UPROPERTY(EditDefaultsOnly, Category = SmartObject, Instanced)
	TArray<USmartObjectBehaviorConfigBase*> BehaviorConfigurations;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = SmartObject)
	FColor DEBUG_DrawColor;
#endif // WITH_EDITORONLY_DATA

	FSmartObjectSlot()
		: Offset(FVector::ZeroVector), Direction(FVector::ForwardVector)
#if WITH_EDITORONLY_DATA
		, DEBUG_DrawColor(FColor::Yellow)
#endif // WITH_EDITORONLY_DATA
	{}
};

/**
 * Helper struct to wrap basic functionalities to store the index of a slot in FSmartObjectConfig
 */
USTRUCT()
struct SMARTOBJECTSMODULE_API FSmartObjectSlotIndex
{
	GENERATED_BODY()

	explicit FSmartObjectSlotIndex(const int32 InSlotIndex = INDEX_NONE) : Index(InSlotIndex) {}

	bool IsValid() const { return Index != INDEX_NONE; }
	void Invalidate() { Index = INDEX_NONE; }

	operator int32() const { return Index; }

	bool operator==(const FSmartObjectSlotIndex& Other) const { return Index == Other.Index; }
	FString Describe() const { return FString::Printf(TEXT("[Slot:%d]"), Index); }

private:
	UPROPERTY(Transient)
	int32 Index = INDEX_NONE;
};

/**
 * Persistent and sharable configuration of a smart object.
 */
USTRUCT(BlueprintType)
struct SMARTOBJECTSMODULE_API FSmartObjectConfig
{
	GENERATED_BODY()
public:

	/**
	 * Retrieves a specific type of behavior configuration for a given slot.
	 * When the slot doesn't provide one or if the provided index is not valid
	 * the search will look in the object default configurations.
	 * 
	 * @param SlotIndex				Index of the slot for which the config is requested
	 * @param ConfigurationClass	Type of the requested behavior configuration
	 * @return The behavior configuration found or null if none are available for the requested type.
	 */
	const USmartObjectBehaviorConfigBase* GetBehaviorConfig(const FSmartObjectSlotIndex& SlotIndex, const TSubclassOf<USmartObjectBehaviorConfigBase>& ConfigurationClass) const;

	/** Returns a view on all the slot configurations */
	TConstArrayView<FSmartObjectSlot> GetSlots() const { return Slots; }

	/** Adds and returns a reference to a defaulted slot (used for testing purposes) */
	FSmartObjectSlot& DebugAddSlot() { return Slots.AddDefaulted_GetRef(); }

	/**
	 * Returns the transform (in world space) of the given slot index.
	 * @param OwnerTransform Transform (in world space) of the slot owner.
	 * @param SlotIndex Index within the list of slots.
	 * @return Transform (in world space) of the slot associated to SlotIndex.
	 * @note Method will ensure on invalid invalid index.
	 */
	TOptional<FTransform> GetSlotTransform(const FTransform& OwnerTransform, const FSmartObjectSlotIndex SlotIndex) const;

	/** Returns the tag query to run on the user tags to accept this configuration */
	const FGameplayTagQuery& GetUserTagFilter() const { return UserTagFilter; }

	/** Returns the tag query to run on the owner tags to accept this configuration */
	const FGameplayTagQuery& GetObjectTagFilter() const { return ObjectTagFilter; }

	/** Returns the list of tags describing the activity associated to this configuration */
	const FGameplayTagContainer& GetActivityTags() const { return ActivityTags; }

	/**
	 *	Performs validation and logs errors if any. An object using an invalid configuration
	 *	will not be registered in the simulation.
	 *	The result of the validation is stored until next validation and can be retrieved using `IsValid`.
	 *	@return true if the configuration is valid
	 */
	bool Validate() const;

	/** Provides a description of the config */
	FString Describe() const;

	/** Returns result of the last validation if `Validate` was called; unset otherwise. */
	TOptional<bool> IsValid() const { return bValid; }

private:
	/**
	 * FSmartObjectConfig is a SparseData type of USmartObjectComponent and its generated code 
	 * requires directed access to the members.
	 */
	friend class USmartObjectComponent;	

	/** Finds first behavior configuration of a given class in the provided list of configurations. */
	static const USmartObjectBehaviorConfigBase* GetBehaviorConfigByType(const TArray<USmartObjectBehaviorConfigBase*>& Configurations, const TSubclassOf<USmartObjectBehaviorConfigBase>& ConfigurationClass);

	/**
	 * Where SmartObject's user needs to stay to be able to activate it. These
	 * will be used by AI to approach the object. Locations are relative to object's location.
	 */
	UPROPERTY(EditDefaultsOnly, Category = SmartObject)
	TArray<FSmartObjectSlot> Slots;

	/** List of behavior configurations of different types provided to SO's user if the slot does not provide one. */
	UPROPERTY(EditDefaultsOnly, Category = SmartObject, Instanced)
	TArray<USmartObjectBehaviorConfigBase*> DefaultBehaviorConfigurations;

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
