// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"

#include "Algo/Transform.h"
#include "RemoteControlField.h"
#include "RemoteControlEntity.h"
#include "UObject/SoftObjectPtr.h"
#include "Templates/PimplPtr.h"
#include "Templates/UnrealTypeTraits.h"

#include "RemoteControlPreset.generated.h"

class AActor;
class IStructSerializerBackend;
class IStructDeserializerBackend;
enum class EPackageReloadPhase : uint8;
struct FPropertyChangedEvent;
struct FRCFieldPathInfo;
struct FRemoteControlActor;
struct FRemoteControlPresetLayout;
class FRemoteControlPresetRebindingManager;
class UBlueprint;
class URemoteControlExposeRegistry;
class URemoteControlBinding;
class URemoteControlPreset;

/** Arguments used to expose an entity (Actor, property, function, etc.) */
struct REMOTECONTROL_API FRemoteControlPresetExposeArgs
{
	FRemoteControlPresetExposeArgs();
	FRemoteControlPresetExposeArgs(FString Label, FGuid GroupId);

	/** (Optional) The label to use for the new exposed entity. */
	FString Label;
	/** (Optional) The group in which to put the field. */
	FGuid GroupId;
};

/**
 * Data cached for every exposed field.
 */
USTRUCT()
struct FRCCachedFieldData
{
	GENERATED_BODY()

	/** The group the field is in. */
	UPROPERTY()
	FGuid LayoutGroupId;

	/** The target that owns this field. */
	UPROPERTY()
	FName OwnerObjectAlias;
};

/**
 * Holds an exposed property and owner objects.
 */
struct UE_DEPRECATED(4.27, "FExposedProperty is deprecated. Please use FRemoteControlProperty::GetBoundObjects to access an exposed property's owner objects.") FExposedProperty;
struct REMOTECONTROL_API FExposedProperty
{
	bool IsValid() const
	{
		return !!Property;
	}

	FProperty* Property;
	TArray<UObject*> OwnerObjects;
};

/**
 * Holds an exposed function, its default parameters and owner objects.
 */
struct UE_DEPRECATED(4.27, "FExposedFunction is deprecated. Please use FRemoteControlFunction::GetBoundObjects to access an exposed function's owner objects.") FExposedFunction;
struct REMOTECONTROL_API FExposedFunction
{
	bool IsValid() const 
	{
		return Function && DefaultParameters;
	}

	UFunction* Function;
	TSharedPtr<class FStructOnScope> DefaultParameters;
	TArray<UObject*> OwnerObjects;
};

/**
 * Represents a group of field and offers operations to operate on the fields inside of that group.
 */
USTRUCT()
struct REMOTECONTROL_API FRemoteControlPresetGroup
{
	GENERATED_BODY()

	FRemoteControlPresetGroup() = default;

	FRemoteControlPresetGroup(FName InName, FGuid InId)
		: Name(InName)
		, Id(MoveTemp(InId))
	{}

	/** Get the fields under this group. */
	const TArray<FGuid>& GetFields() const;

	/** Get the fields under this group (Non-const)*/
	TArray<FGuid>& AccessFields();

	friend bool operator==(const FRemoteControlPresetGroup& LHS, const FRemoteControlPresetGroup& RHS)
	{
		return LHS.Id == RHS.Id;
	}
 
public:
	/** Name of this group. */
	UPROPERTY()
	FName Name;

	/** This group's ID. */
	UPROPERTY()
	FGuid Id;

private:
	/** The list of exposed fields under this group. */
	UPROPERTY()
	TArray<FGuid> Fields;
};

/** Layout that holds groups of fields. */
USTRUCT()
struct REMOTECONTROL_API FRemoteControlPresetLayout
{
	GENERATED_BODY()

	/** Arguments for swapping fields across groups.  */
	struct FFieldSwapArgs
	{
		FGuid OriginGroupId;
		FGuid TargetGroupId;
		FGuid DraggedFieldId;
		FGuid TargetFieldId;
	};

	FRemoteControlPresetLayout() = default;
	FRemoteControlPresetLayout(URemoteControlPreset* OwnerPreset);

	/** Get or create the default group. */
	FRemoteControlPresetGroup& GetDefaultGroup();

	/**
	 * Get a group by searching by ID.
	 * @param GroupId the id to use.
	 * @return A pointer to the found group or nullptr.
	 */
	FRemoteControlPresetGroup* GetGroup(FGuid GroupId); 

	/**
	 *
	 * Get a group by searching by name.
	 * @param GroupName the name to use.
	 * @return A pointer to the found group or nullptr.
	 */
	FRemoteControlPresetGroup* GetGroupByName(FName GroupName);

	/** Create a group by giving it a name and ID */
	UE_DEPRECATED(4.27, "This function was deprecated, use the overload that doesn't accept a group id.")
	FRemoteControlPresetGroup& CreateGroup(FName GroupName, FGuid GroupId);

	/** Create a group in the layout with a given name. */
	FRemoteControlPresetGroup& CreateGroup(FName GroupName = NAME_None);

	/** Find the group that holds the specified field. */
	/**
	 * Search for a group that contains a certain field.
	 * @param FieldId the field to search a group for.
	 * @return A pointer to the found group or nullptr.
	 */
	FRemoteControlPresetGroup* FindGroupFromField(FGuid FieldId);

	/**
	 * Move field to a group.
	 * @param FieldId the field to move.
	 * @param TargetGroupId the group to move the field in.
	 * @return whether the operation was successful.
	 */
	bool MoveField(FGuid FieldId, FGuid TargetGroupId);

	/** Swap two groups. */
	void SwapGroups(FGuid OriginGroupId, FGuid TargetGroupId);

	/** Swap fields across groups or in the same one. */
	void SwapFields(const FFieldSwapArgs& FieldSwapArgs);

	/** Delete a group from the layout. */
	void DeleteGroup(FGuid GroupId);

	/** Rename a group in the layout. */
	void RenameGroup(FGuid GroupId, FName NewGroupName);

	/** Get this layout's groups. */
	const TArray<FRemoteControlPresetGroup>& GetGroups() const;

	/** 
	 * Non-Const getter for this layout's groups. 
	 * @Note Use carefully, as adding/removing groups should be done using their respective methods.
	 */
	TArray<FRemoteControlPresetGroup>& AccessGroups();

	/** Append a field to the group's field list. */
	void AddField(FGuid GroupId, FGuid FieldId);

	/** Insert a field in the group. */
	void InsertFieldAt(FGuid GroupId, FGuid FieldId, int32 Index);

	/** Remove a field using the field's name. */
	void RemoveField(FGuid GroupId, FGuid FieldId);

	/** Remove a field at a provided index. */
	void RemoveFieldAt(FGuid GroupId, int32 Index);

	/** Get the preset that owns this layout. */
	URemoteControlPreset* GetOwner();

	// Layout operation delegates
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnGroupAdded, const FRemoteControlPresetGroup& /*NewGroup*/);
	FOnGroupAdded& OnGroupAdded() { return OnGroupAddedDelegate; }

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnGroupDeleted, FRemoteControlPresetGroup/*DeletedGroup*/);
	FOnGroupDeleted& OnGroupDeleted() { return OnGroupDeletedDelegate; }

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnGroupOrderChanged, const TArray<FGuid>& /*GroupIds*/);
	FOnGroupOrderChanged& OnGroupOrderChanged() { return OnGroupOrderChangedDelegate; }

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnGroupRenamed, const FGuid& /*GroupId*/, FName /*NewName*/);
	FOnGroupRenamed& OnGroupRenamed() { return OnGroupRenamedDelegate; }

	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnFieldAdded, const FGuid& /*GroupId*/, const FGuid& /*FieldId*/, int32 /*FieldPosition*/);
	FOnFieldAdded& OnFieldAdded() { return OnFieldAddedDelegate; }

	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnFieldDeleted, const FGuid& /*GroupId*/, const FGuid& /*FieldId*/, int32 /*FieldPosition*/);
	FOnFieldDeleted& OnFieldDeleted() { return OnFieldDeletedDelegate; }

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnFieldOrderChanged, const FGuid& /*GroupId*/, const TArray<FGuid>& /*FieldIds*/);
	FOnFieldOrderChanged& OnFieldOrderChanged() { return OnFieldOrderChangedDelegate; }

private:
	/** Create a group by providing a name and ID. */
	FRemoteControlPresetGroup& CreateGroupInternal(FName GroupName, FGuid GroupId);

	/** The list of groups under this layout. */
	UPROPERTY()
	TArray<FRemoteControlPresetGroup> Groups;

	/** The preset that owns this layout. */
	UPROPERTY()
	TWeakObjectPtr<URemoteControlPreset> Owner = nullptr;

	// Layout operation delegates
	FOnGroupAdded OnGroupAddedDelegate;
	FOnGroupDeleted OnGroupDeletedDelegate;
	FOnGroupOrderChanged OnGroupOrderChangedDelegate;
	FOnGroupRenamed OnGroupRenamedDelegate;
	FOnFieldAdded OnFieldAddedDelegate;
	FOnFieldDeleted OnFieldDeletedDelegate;
	FOnFieldOrderChanged OnFieldOrderChangedDelegate;
};

/**
 * Represents objects grouped under a single alias that contain exposed functions and properties.
 */
struct UE_DEPRECATED(4.27, "FRemoteControlTarget is deprecated. Expose properties directly on a remote control preset instead.") FRemoteControlTarget;
USTRUCT()
struct REMOTECONTROL_API FRemoteControlTarget
{
public:
	GENERATED_BODY()

	FRemoteControlTarget() = default;

	FRemoteControlTarget(class URemoteControlPreset* InOwner)
		: Owner(InOwner)
		{}

	/**
	* Expose a property in this target.
	* @param FieldPathInfo the path data from the owner object, including component chain, to this field. (ie. LightComponent0.Intensity)
	* @param DesiredDisplayName the display name desired for this control. If the name is not unique preset-wise, a number will be appended.
	*/
	UE_DEPRECATED(4.27, "A component hierarchy is no longer needed to expose properties, use the overloaded function instead.")
	TOptional<FRemoteControlProperty> ExposeProperty(FRCFieldPathInfo FieldPathInfo, TArray<FString> ComponentHierarchy, const FString& DesiredDisplayName, FGuid GroupId = FGuid());

	/**
	 * Expose a property in this target.
	 * @param FieldPathInfo the path data from the owner object, including component chain, to this field. (ie. LightComponent0.Intensity)
	 * @param DesiredDisplayName the display name desired for this control. If the name is not unique preset-wise, a number will be appended.
	 */
	TOptional<FRemoteControlProperty> ExposeProperty(FRCFieldPathInfo FieldPathInfo, const FString& DesiredDisplayName, FGuid GroupId = FGuid(), bool bAppendAliasToLabel = false);

	/**
	 * Expose a function in this target.
	 * @param RelativeFieldPath the path from the owner object  to this field. (ie. Subcomponent.GetName)
	 * @param DesiredDisplayName the display name desired for this control. If the name is not unique target-wide, a number will be appended.
	 */
	TOptional<FRemoteControlFunction> ExposeFunction(FString RelativeFieldPath, const FString& DesiredDisplayName, FGuid GroupId = FGuid(), bool bAppendAliasToLabel = false);

	/**
	 * Unexpose a field from this target.
	 * @param TargetFieldId The ID of the field to remove.
	 */
	void Unexpose(FGuid TargetFieldId);

	/**
	 * Find a field label using a property name.
	 * @param PropertyName the FName of the property to find.
	 * @return the field's label or an empty name if not found.
	 */
	FName FindFieldLabel(FName FieldName) const;

	/**
	 * Find a field label using a a field path info.
	 * @param Path the FieldPathInfo of the field to find.
	 * @return the field's label or an empty name if not found.
	 */
	FName FindFieldLabel(const FRCFieldPathInfo& Path) const;

	/**
	 * Get a property using its label.
	 * @param FieldLabel the property's label.
	 * @return The target property if found.
	 */
	TOptional<FRemoteControlProperty> GetProperty(FGuid PropertyId) const;

	/**
	 * Get an exposed function using it's name.
	 * @param FunctionName the function name to query.
	 * @return The function if found.
	 */
	TOptional<FRemoteControlFunction> GetFunction(FGuid FunctionId) const;

	/**
	 * Resolve a remote controlled property to its FProperty and owner objects.
	 * @param PropertyLabel the label of the remote controlled property.
	 * @return The resolved exposed property if found.
	 */
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	TOptional<FExposedProperty> ResolveExposedProperty(FGuid PropertyId) const;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	/**
	 * Resolve a remote controlled function to its UFunction and owner objects.
	 * @param FunctionLabel the label of the remote controlled function.
	 * @return The resolved exposed function if found.
	 */
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	TOptional<FExposedFunction> ResolveExposedFunction(FGuid FunctionId) const;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	/**
	 * Resolve the target bindings to return the target's underlying objects.
	 * @return The target's underlying objects.
	 */
	TArray<UObject*> ResolveBoundObjects() const;

	/**
	 * Adds objects to the target's underlying objects, binding them to the target's alias.
	 * @param ObjectsToBind The objects to bind.
	 */
	void BindObjects(const TArray<UObject*>& ObjectsToBind);

	/**
	 * Verifies if the list of objects provided is bound under this target's alias.
	 * @param ObjectsToTest The objects to validate.
	 * @return true if the target has  underlying objects.
	 */
	bool HasBoundObjects(const TArray<UObject*>& ObjectsToTest) const;

	/**
	 * Verifies if an object is bound under this target's alias.
	 * @param ObjectToTest The objects to validate.
	 * @return true if the target has the object bound.
	 */
	bool HasBoundObject(const UObject* ObjectToTest) const;

	/**
	 * Returns whether the provided list of objects can be bound under this target.
	 * @param ObjectsToTest The object list.
	 * @return Whether the objects can be bound.
	 */
	bool CanBindObjects(const TArray<UObject*>& ObjectsToTest) const;

private:

	/** Remove a field from the specified fields array. */
	template <typename Type> 
	void RemoveField(TSet<Type>& Fields, FGuid FieldId)
	{
		Fields.RemoveByHash(GetTypeHash(FieldId), FieldId);
	}

	FProperty* FindPropertyRecursive(UStruct* Container, TArray<FString>& DesiredPropertyPath) const;

public:
	/**
	 * The common class of the target's underlying objects.
	 */
	UPROPERTY()
	UClass* Class = nullptr;

	/**
	 * The target's exposed functions.
	 */
	UPROPERTY()
	TSet<FRemoteControlFunction> ExposedFunctions;

	/**
	 * The target's exposed properties.
	 */
	UPROPERTY()
	TSet<FRemoteControlProperty> ExposedProperties;

	/**
	 * The alias for this target.
	 */
	UPROPERTY()
	FName Alias;
private:
	/**
	 * The objects bound under the target's alias.
	 */
	UPROPERTY()
	TArray<FSoftObjectPath> Bindings;

	/**
	 * The preset that owns this target.
	 */
	UPROPERTY()
	TWeakObjectPtr<URemoteControlPreset> Owner;

	friend URemoteControlPreset;
};

/**
 * Holds targets that contain exposed functions and properties.
 */
UCLASS(BlueprintType, EditInlineNew)
class REMOTECONTROL_API URemoteControlPreset : public UObject
{
public:
	GENERATED_BODY()
	
	/** Callback for post remote control preset load, called by URemoteControlPreset::PostLoad function */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPostLoadRemoteControlPreset, URemoteControlPreset* /* InPreset */);
	static FOnPostLoadRemoteControlPreset OnPostLoadRemoteControlPreset;
	
	URemoteControlPreset();

	//~ Begin UObject interface
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual void BeginDestroy() override;
	
#if WITH_EDITOR
	void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent);
#endif /*WITH_EDITOR*/
	//~ End UObject interface

	/**
	 * Get this preset's unique ID.
	 */
	const FGuid& GetPresetId() const { return PresetId; }

	/**
	 * Get this preset's targets.
	 */
	UE_DEPRECATED(4.27, "FRemoteControlTarget is deprecated, use URemoteControlPreset::Bindings instead.")
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	TMap<FName, FRemoteControlTarget>& GetRemoteControlTargets() { return RemoteControlTargets; }
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	/**
	 * Get this preset's targets.
	 */
	UE_DEPRECATED(4.27, "FRemoteControlTarget is deprecated, use URemoteControlPreset::Bindings instead.")
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const TMap<FName, FRemoteControlTarget>& GetRemoteControlTargets() const { return RemoteControlTargets; }
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	/**
	 * Get the target that owns this exposed field.
	 */
	UE_DEPRECATED(4.27, "FRemoteControlTarget is deprecated, access the entity's bindings by accessing FRemoteControlEntity::Bindings.")
	FName GetOwnerAlias(FGuid FieldId) const;

	/**
	 * Get the ID for a field using its label.
	 * @param FieldLabel the field's label.
	 * @return the field's id, or an invalid GUID if not found.
	 */
	UE_DEPRECATED(4.27, "Use URemoteControlPreset::GetExposedEntityId instead.")
	FGuid GetFieldId(FName FieldLabel) const;

	/**
	 * Expose an actor on this preset.
	 * @param Actor the actor to expose.
	 * @param Args The arguments used to expose the actor.
	 * @return The exposed actor.
	 */
	TWeakPtr<FRemoteControlActor> ExposeActor(AActor* Actor, FRemoteControlPresetExposeArgs Args = FRemoteControlPresetExposeArgs());

	/**
	 * Expose a property on this preset.
	 * @param Object the object that holds the property.
	 * @param FieldPath The name/path to the property.
	 * @param Args Optional arguments used to expose the property.
	 * @return The exposed property.
	 */
	TWeakPtr<FRemoteControlProperty> ExposeProperty(UObject* Object, FRCFieldPathInfo FieldPath, FRemoteControlPresetExposeArgs Args = FRemoteControlPresetExposeArgs());
	
	/**
	 * Expose a function on this preset.
	 * @param Object the object that holds the property.
	 * @param Function The function to expose.
	 * @param Args Optional arguments used to expose the property.
	 * @return The exposed function.
	 */
	TWeakPtr<FRemoteControlFunction> ExposeFunction(UObject* Object, UFunction* Function, FRemoteControlPresetExposeArgs Args = FRemoteControlPresetExposeArgs());

	/**
	 * Get the exposed entities of a certain type.
	 */
	template <typename ExposableEntityType>
	TArray<TWeakPtr<const ExposableEntityType>> GetExposedEntities() const
	{
		static_assert(TIsDerivedFrom<ExposableEntityType, FRemoteControlEntity>::Value, "ExposableEntityType must derive from FRemoteControlEntity.");
		TArray<TWeakPtr<const ExposableEntityType>> ReturnedEntities;
		TArray<TSharedPtr<const FRemoteControlEntity>> Entities = GetEntities(ExposableEntityType::StaticStruct());
		Algo::Transform(Entities, ReturnedEntities,
			[](const TSharedPtr<const FRemoteControlEntity>& Entity)
			{
				return StaticCastSharedPtr<const ExposableEntityType>(Entity);
			});
		return ReturnedEntities;
	}

	/**
	 * Get the exposed entities of a certain type.
	 */
	template <typename ExposableEntityType>
	TArray<TWeakPtr<ExposableEntityType>> GetExposedEntities()
	{
		static_assert(TIsDerivedFrom<ExposableEntityType, FRemoteControlEntity>::Value, "ExposableEntityType must derive from FRemoteControlEntity.");

		TArray<TWeakPtr<ExposableEntityType>> ReturnedEntities;
		TArray<TSharedPtr<FRemoteControlEntity>> Entities = GetEntities(ExposableEntityType::StaticStruct());
		Algo::Transform(Entities, ReturnedEntities,
			[](const TSharedPtr<FRemoteControlEntity>& Entity)
			{
				return StaticCastSharedPtr<ExposableEntityType>(Entity);
			});
		return ReturnedEntities;
	}

	/**
	 * Get a copy of an exposed entity on the preset.
	 * @param ExposedEntityId The id of the entity to get.
	 * @note ExposableEntityType must derive from FRemoteControlEntity.
	 */
	template <typename ExposableEntityType = FRemoteControlEntity>
	TWeakPtr<const ExposableEntityType> GetExposedEntity(const FGuid& ExposedEntityId) const
	{
		static_assert(TIsDerivedFrom<ExposableEntityType, FRemoteControlEntity>::Value, "ExposableEntityType must derive from FRemoteControlEntity.");
		return StaticCastSharedPtr<const ExposableEntityType>(FindEntityById(ExposedEntityId, ExposableEntityType::StaticStruct()));
	}

	/**
	 * Get a pointer to an exposed entity on the preset.
	 * @param ExposedEntityId The id of the entity to get.
	 * @note ExposableEntityType must derive from FRemoteControlEntity.
	 */
	template <typename ExposableEntityType = FRemoteControlEntity>
	TWeakPtr<ExposableEntityType> GetExposedEntity(const FGuid& ExposedEntityId)
	{
		static_assert(TIsDerivedFrom<ExposableEntityType, FRemoteControlEntity>::Value, "ExposableEntityType must derive from FRemoteControlEntity.");
		return StaticCastSharedPtr<ExposableEntityType>(FindEntityById(ExposedEntityId, ExposableEntityType::StaticStruct()));
	}

	/** Get the type of an exposed entity by querying with its id. (ie. FRemoteControlActor) */
	const UScriptStruct* GetExposedEntityType(const FGuid& ExposedEntityId) const;

	/** Returns whether an entity is exposed on the preset. */
	bool IsExposed(const FGuid& ExposedEntityId) const;

	/**
	 * Change the label of an entity.
	 * @param ExposedEntityId the id of the entity to rename.
	 * @param NewLabel the new label to assign to the entity.
	 * @return The assigned label, which might be suffixed if the label already exists in the registry, 
	 *         or NAME_None if the entity was not found.
	 */
	FName RenameExposedEntity(const FGuid& ExposedEntityId, FName NewLabel);

	/**
	 * Get the ID of an exposed entity using its label.
	 * @return an invalid guid if the exposed entity was not found.
	 */
	FGuid GetExposedEntityId(FName Label) const;

	/**
	 * Get a field ptr using it's id.
	 */
	UE_DEPRECATED(4.27, "Use GetExposedEntity<FRemoteControlField> instead.")
	TOptional<FRemoteControlField> GetField(FGuid FieldId) const;

	 /**
	  * Get an exposed function using its label.
	  */
	UE_DEPRECATED(4.27, "Use GetExposedEntity<FRemoteControlFunction> instead.")
	TOptional<FRemoteControlFunction> GetFunction(FName FunctionLabel) const;

	/** 
	 * Get an exposed property using its label.
	 */
	UE_DEPRECATED(4.27, "Use GetExposedEntity<FRemoteControlProperty> instead.")
	TOptional<FRemoteControlProperty> GetProperty(FName PropertyLabel) const;

	/**
	 * Get an exposed function using its id.
	 */
	UE_DEPRECATED(4.27, "Use GetExposedEntity<FRemoteControlFunction> instead.")
	TOptional<FRemoteControlFunction> GetFunction(FGuid FunctionId) const;

	/**
	 * Get an exposed property using its id.
	 */
	UE_DEPRECATED(4.27, "Use GetExposedEntity<FRemoteControlProperty> instead.")
	TOptional<FRemoteControlProperty> GetProperty(FGuid PropertyId) const;

	/**
	 * Rename a field.
	 * @param OldFieldLabel the target field's label.
	 * @param NewFieldLabel the field's new label.
	 */
	UE_DEPRECATED(4.27, "Use RenameExposedEntity instead.")
	void RenameField(FName OldFieldLabel, FName NewFieldLabel);

	/**
	 * Resolve a remote controlled property to its FProperty and owner objects.
	 * @param Alias the target's alias that contains the remote controlled property.
	 * @param PropertyLabel the label of the remote controlled property.
	 * @return The resolved exposed property if found.
	 */
	UE_DEPRECATED(4.27, "Use FRemoteControlProperty::GetProperty and FRemoteControlProperty::ResolveFieldOwners instead.")
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	TOptional<FExposedProperty> ResolveExposedProperty(FName PropertyLabel) const;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	/**
	 * Resolve a remote controlled function to its UFunction and owner objects.
	 * @param Alias the target's alias that contains the remote controlled function.
	 * @param FunctionLabel the label of the remote controlled function.
	 * @return The resolved exposed function if found.
	 */
	UE_DEPRECATED(4.27, "Use FRemoteControlFunction::ResolveFieldOwners instead.")
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	TOptional<FExposedFunction> ResolveExposedFunction(FName FunctionLabel) const;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	/**
	 * Unexpose an entity from the preset.
	 * @param EntityLabel The label of the entity to unexpose.
	 */
	void Unexpose(FName EntityLabel);

	/**
	 * Unexpose an entity from the preset.
	 * @param EntityId the entity's id.
	 */
	void Unexpose(const FGuid& EntityId);

	/**
	 * Create a new target under this preset.
	 * @param TargetObjects The objects to group under a common alias for the target.
	 * @return The new target's alias if successful.
	 * @note A target must be created with at least one object and they must have a common base class.
	 */
	UE_DEPRECATED(4.27, "Targets are deprecated in favor of exposing directly on the preset. You do not need to create a target beforehand.")
	FName CreateTarget(const TArray<UObject*>& TargetObjects);

	/**
	 * Create a new target under this preset.
	 * @param TargetObjects The objects to group under a common alias for the target.
	 * @return The new target.
	 * @note A target must be created with at least one object and they must have a common base class.
	 */
	UE_DEPRECATED(4.27, "Targets are deprecated in favor of exposing directly on the preset. You do not need to create a target beforehand.")
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FRemoteControlTarget& CreateAndGetTarget(const TArray<UObject*>& TargetObjects);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	/**
	 * Remove a target from the preset.
	 * @param TargetName The target to delete.
	 */
	UE_DEPRECATED(4.27, "Targets are deprecated, you do not need to delete them anymore.")
	void DeleteTarget(FName TargetName);

	/**
	 * Rename a target.
	 * @param TargetName The name of the target to rename.
	 * @param NewTargetName The new target's name.
	 */
	UE_DEPRECATED(4.27, "Targets are deprecated in favor of bindings, you can now change the name of the binding directly.")
	void RenameTarget(FName TargetName, FName NewTargetName);

	/** Cache this preset's layout data. */
	void CacheLayoutData();

	/** Resolves exposed property/function bounded objects */
	UE_DEPRECATED(4.27, "ResolvedBoundObjects is deprecated, you can now resolve bound objects using FRemoteControlEntity.")
	TArray<UObject*> ResolvedBoundObjects(FName FieldLabel);

	/**
	 * Attempt to rebind all currently unbound properties.
	 */
	void RebindUnboundEntities();

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPresetEntityEvent, URemoteControlPreset* /*Preset*/, const FGuid& /*EntityId*/);
	FOnPresetEntityEvent& OnEntityExposed() { return OnEntityExposedDelegate; }
	FOnPresetEntityEvent& OnEntityUnexposed() { return OnEntityUnexposedDelegate; }

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPresetExposedPropertiesModified, URemoteControlPreset* /*Preset*/, const TSet<FGuid>& /* ModifiedProperties */);
	/**
	 *  Delegate called with the list of exposed property that were modified in the last frame.
	 */
	FOnPresetExposedPropertiesModified& OnExposedPropertiesModified() { return OnPropertyChangedDelegate; }

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPresetEntitiesUpdatedEvent, URemoteControlPreset* /*Preset*/, const TSet<FGuid>& /* Modified Entities */);
	/**
	 * Delegate called when the exposed entity wrapper itself is updated (ie. binding change, rename)  
	 */
	FOnPresetEntitiesUpdatedEvent& OnEntitiesUpdated() { return OnEntitiesUpdatedDelegate; }
	
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPresetPropertyExposed, URemoteControlPreset* /*Preset*/, FName /*ExposedLabel*/);
	UE_DEPRECATED(4.27, "This delegate is deprecated, use OnEntityExposed instead.")
	FOnPresetPropertyExposed& OnPropertyExposed() { return OnPropertyExposedDelegate; }

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPresetPropertyUnexposed, URemoteControlPreset* /*Preset*/, FName /*ExposedLabel*/);
	UE_DEPRECATED(4.27, "This delegate is deprecated, use OnEntityUnexposed instead.")
	FOnPresetPropertyUnexposed& OnPropertyUnexposed() { return OnPropertyUnexposedDelegate; }

	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnPresetFieldRenamed, URemoteControlPreset* /*Preset*/, FName /*OldExposedLabel*/, FName /**NewExposedLabel*/);
	FOnPresetFieldRenamed& OnFieldRenamed() { return OnPresetFieldRenamed; }

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPresetMetadataModified, URemoteControlPreset* /*Preset*/);
	FOnPresetMetadataModified& OnMetadataModified() { return OnMetadataModifiedDelegate; }

	DECLARE_MULTICAST_DELEGATE_FourParams(FOnActorPropertyModified, URemoteControlPreset* /*Preset*/, FRemoteControlActor& /*Actor*/, UObject* /*ModifiedObject*/, FProperty* /*MemberProperty*/);
	FOnActorPropertyModified& OnActorPropertyModified() { return OnActorPropertyModifiedDelegate; }

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPresetLayoutModified, URemoteControlPreset* /*Preset*/);
	FOnPresetLayoutModified& OnPresetLayoutModified() { return OnPresetLayoutModifiedDelegate; }

	UE_DEPRECATED(4.27, "This function is deprecated.")
	void NotifyExposedPropertyChanged(FName PropertyLabel);

	virtual void Serialize(FArchive& Ar) override;

public:
	/** The visual layout for this preset. */
	UPROPERTY()
	FRemoteControlPresetLayout Layout;

	/** This preset's metadata. */
	UPROPERTY()
	TMap<FString, FString> Metadata;

	/** This preset's list of objects that are exposed or that have exposed fields. */
	UPROPERTY(EditAnywhere, Category = "Remote Control Preset")
	TArray<URemoteControlBinding*> Bindings;

private:
	/** Generate a unique alias for this target. */
	FName GenerateAliasForObjects(const TArray<UObject*>& Objects);
	
	/** Generate a label for a field that is unique preset-wide. */
	FName GenerateUniqueFieldLabel(FName Alias, const FString& BaseName, bool bAppendAlias);

	/** Holds information about an exposed field. */
	struct FExposeInfo
	{
		FName Alias;
		FGuid FieldId;
		FGuid LayoutGroupId;
	};

	//~ Callbacks called by the object targets
	void OnExpose(const FExposeInfo& Info);
	void OnUnexpose(FGuid UnexposedFieldId);
	
	//~ Cache operations.
	void CacheFieldsData();
	void CacheFieldLayoutData();
	
	//~ Register/Unregister delegates
	void RegisterDelegates();
	void UnregisterDelegates();
	
	//~ Keep track of any property change to notify if one of the exposed property has changed
	void OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);
	void OnPreObjectPropertyChanged(UObject* Object, const class FEditPropertyChain& PropertyChain);

#if WITH_EDITOR	
	//~ Handle events that can incur bindings to be modified.
	void OnActorDeleted(AActor* Actor);
	void OnPieEvent(bool);
	void OnReplaceObjects(const TMap<UObject*, UObject*>& ReplacementObjectMap);
	void OnMapChange(uint32 /*MapChangeFlags*/);
	void OnBlueprintRecompiled(UBlueprint* Blueprint);

	/** Handles a package reloaded, used to detect a multi-user session being joined in order to update entities. */
	void OnPackageReloaded(EPackageReloadPhase Phase, FPackageReloadedEvent* Event);
#endif

	//~ Frame events handlers.
	void OnBeginFrame();
	void OnEndFrame();
	
	/** Get a field ptr using it's id. */
	FRemoteControlField* GetFieldPtr(FGuid FieldId);

	//~ Utility functions to update the asset format.
	void ConvertFieldsToRemoveComponentChain();
	void ConvertFieldsToEntities();
	void ConvertTargetsToBindings();

	//~ Helper methods that interact with the expose registry.,
	TSharedPtr<const FRemoteControlEntity> FindEntityById(const FGuid& EntityId, const UScriptStruct* EntityType = FRemoteControlEntity::StaticStruct()) const;
	TSharedPtr<FRemoteControlEntity> FindEntityById(const FGuid& EntityId, const UScriptStruct* EntityType = FRemoteControlEntity::StaticStruct());
	TArray<TSharedPtr<FRemoteControlEntity>> GetEntities(UScriptStruct* EntityType);
	TArray<TSharedPtr<const FRemoteControlEntity>> GetEntities(UScriptStruct* EntityType) const;

	/** Expose an entity in the registry. */
	TSharedPtr<FRemoteControlEntity> Expose(FRemoteControlEntity&& Entity, UScriptStruct* EntityType, const FGuid& GroupId);
	
	/** Try to get a binding and creates a new one if it doesn't exist. */
	URemoteControlBinding* FindOrAddBinding(const TSoftObjectPtr<UObject>& Object);

	/** Handler called upon an entity being modified. */
	void OnEntityModified(const FGuid& EntityId);

	/** Initialize entities metadata based on the module's externally registered initializers. */
	void InitializeEntitiesMetadata();
	
	/** Initialize an entity's metadata based on the module's externally registered initializers. */
	void InitializeEntityMetadata(const TSharedPtr<FRemoteControlEntity>& Entity);

	/** Register delegates for all exposed entities. */
	void RegisterEntityDelegates();

	/** Register an event triggered when the exposed function's owner blueprint is compiled. */
	void RegisterOnCompileEvent(const TSharedPtr<FRemoteControlFunction>& RCFunction);

	/** Create a property watcher that will trigger the property change delegate upon having a different value from the last frame. */
	void CreatePropertyWatcher(const TSharedPtr<FRemoteControlProperty>& RCProperty);

	/** Returns whether a given property should have a watcher that checks for property changes across frames. */
	bool PropertyShouldBeWatched(const TSharedPtr<FRemoteControlProperty>& RCProperty) const;

	/** Create property watchers for exposed properties that need them. */
	void CreatePropertyWatchers();
	
private:
	/** Preset unique ID */
	UPROPERTY(AssetRegistrySearchable)
	FGuid PresetId;

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	/** The mappings of alias to targets. */
	UPROPERTY()
	TMap<FName, FRemoteControlTarget> RemoteControlTargets;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	/** The cache for information about an exposed field. */
	UPROPERTY(Transient)
	TMap<FGuid, FRCCachedFieldData> FieldCache;

	/** Map of Field Name to GUID. */
	UPROPERTY(Transient)
	TMap<FName, FGuid> NameToGuidMap;

	UPROPERTY(Instanced)
	/** Holds exposed entities on the preset. */
	URemoteControlExposeRegistry* Registry = nullptr;

	/** Delegate triggered when an entity is exposed. */
	FOnPresetEntityEvent OnEntityExposedDelegate;
	/** Delegate triggered when an entity is unexposed from the preset. */
	FOnPresetEntityEvent OnEntityUnexposedDelegate;
	/** Delegate triggered when entities are modified and may need to be re-resolved. */
	FOnPresetEntitiesUpdatedEvent OnEntitiesUpdatedDelegate;
	/** Delegate triggered when an exposed property value has changed. */
	FOnPresetExposedPropertiesModified OnPropertyChangedDelegate;
	/** Delegate triggered when a new property has been exposed. */
	FOnPresetPropertyExposed OnPropertyExposedDelegate;
	/** Delegate triggered when a property has been unexposed. */
	FOnPresetPropertyUnexposed OnPropertyUnexposedDelegate;
	/** Delegate triggered when a field has been renamed. */
	FOnPresetFieldRenamed OnPresetFieldRenamed;
	/** Delegate triggered when the preset's metadata has been modified. */
	FOnPresetMetadataModified OnMetadataModifiedDelegate;
	/** Delegate triggered when an exposed actor's property is modified. */
	FOnActorPropertyModified OnActorPropertyModifiedDelegate;
	/** Delegate triggered when the layout is modified. */
	FOnPresetLayoutModified OnPresetLayoutModifiedDelegate;

	struct FPreObjectsModifiedCache
	{
		TArray<UObject*> Objects;
		FProperty* Property;
		FProperty* MemberProperty;
	};

	/** Caches object modifications during a frame. */
	TMap<FGuid, FPreObjectsModifiedCache> PreObjectsModifiedCache;
	TMap<FGuid, FPreObjectsModifiedCache> PreObjectsModifiedActorCache;

	/** Cache properties that were modified during a frame. */
	TSet<FGuid> PerFrameModifiedProperties;
	
	/** Cache entities updated during a frame. */
	TSet<FGuid> PerFrameUpdatedEntities; 

	/** Whether there is an ongoing remote modification happening. */
	bool bOngoingRemoteModification = false;

	/** Holds manager that handles rebinding unbound entities upon load or map change. */
	TPimplPtr<FRemoteControlPresetRebindingManager> RebindingManager;

	/**
	 * Property watcher that triggers a delegate when the watched property is modified.
	 */
	struct FRCPropertyWatcher
	{
		FRCPropertyWatcher(const TSharedPtr<FRemoteControlProperty>& InWatchedProperty, FSimpleDelegate&& InOnWatchedValueChanged);

		/** Checks if the property value has changed since the last frame and updates the last frame value. */
		void CheckForChange();

	private:
		/** Optionally resolve the property path if possible. */
		TOptional<FRCFieldResolvedData> GetWatchedPropertyResolvedData() const;
		/** Store the latest property value in LastFrameValue. */
		void SetLastFrameValue(const FRCFieldResolvedData& ResolvedData);
	private:
		/** Delegate called when the watched property changes values. */
		FSimpleDelegate OnWatchedValueChanged;
		/** Weak pointer to the remote control property. */
		TWeakPtr<FRemoteControlProperty> WatchedProperty;
		/** Latest property value as bytes. */
		TArray<uint8> LastFrameValue;
	};
	/** Map of property watchers that should trigger the RC property change delegate upon change. */
	TMap<FGuid, FRCPropertyWatcher> PropertyWatchers;

	/** Frame counter for delaying property change checks. */
	int32 PropertyChangeWatchFrameCounter = 0;

#if WITH_EDITOR
	/** List of blueprints for which we have registered events. */
	TSet<TWeakObjectPtr<UBlueprint>> BlueprintsWithRegisteredDelegates;
#endif

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	friend FRemoteControlTarget;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	friend FRemoteControlPresetLayout;
	friend FRemoteControlEntity;
};
