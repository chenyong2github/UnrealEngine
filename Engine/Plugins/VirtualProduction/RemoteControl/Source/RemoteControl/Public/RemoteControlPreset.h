// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"

#include "RemoteControlField.h"
#include "UObject/SoftObjectPtr.h"
#include "RemoteControlPreset.generated.h"

class IStructSerializerBackend;
class IStructDeserializerBackend;
class URemoteControlPreset;
struct FRemoteControlPresetLayout;

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

	/** Fetch a group using its id. */
	FRemoteControlPresetGroup* GetGroup(FGuid GroupId); 

	/** Fetch a group using its name. */
	FRemoteControlPresetGroup* GetGroupByName(FName GroupName);
	
	/** Create a group in the layout. */
	FRemoteControlPresetGroup& CreateGroup(FName GroupName, FGuid GroupId);

	/** Create a group in the layout. */
	FRemoteControlPresetGroup& CreateGroup();

	/** Find the group that holds the specified field. */
	FRemoteControlPresetGroup* FindGroupFromField(FGuid FieldId);

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
	TOptional<FRemoteControlProperty> ExposeProperty(FRCFieldPathInfo FieldPathInfo, TArray<FString> ComponentHierarchy, const FString& DesiredDisplayName, FGuid GroupId = FGuid());

	/**
	 * Expose a function in this target.
	 * @param RelativeFieldPath the path from the owner object  to this field. (ie. Subcomponent.GetName)
	 * @param DesiredDisplayName the display name desired for this control. If the name is not unique target-wide, a number will be appended.
	 */
	TOptional<FRemoteControlFunction> ExposeFunction(FString RelativeFieldPath, const FString& DesiredDisplayName, FGuid GroupId = FGuid());

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
	TOptional<FExposedProperty> ResolveExposedProperty(FGuid PropertyId) const;

	/**
	 * Resolve a remote controlled function to its UFunction and owner objects.
	 * @param FunctionLabel the label of the remote controlled function.
	 * @return The resolved exposed function if found.
	 */
	TOptional<FExposedFunction> ResolveExposedFunction(FGuid FunctionId) const;

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
	bool HasBoundObjects(const TArray<UObject*>& ObjectsToTest);

	/**
	 * Returns whether the provided list of objects can be bound under this target.
	 * @param ObjectsToTest The object list.
	 * @return Whether the objects can be bound.
	 */
	bool CanBindObjects(const TArray<UObject*>& ObjectsToTest);

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
UCLASS(BlueprintType)
class REMOTECONTROL_API URemoteControlPreset : public UObject
{
public:
	GENERATED_BODY()

	URemoteControlPreset();

	//~ Begin UObject interface
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	virtual void PostRename(UObject* OldOuter, const FName OldName) override;
	//~ End UObject interface
		                                          
	/**
	 * Get this preset's targets.
	 */
	TMap<FName, FRemoteControlTarget>& GetRemoteControlTargets() { return RemoteControlTargets; }

	/**
	 * Get this preset's targets.
	 */
	const TMap<FName, FRemoteControlTarget>& GetRemoteControlTargets() const { return RemoteControlTargets; }

	/**
	 * Get the target that owns this exposed field.
	 */
	FName GetOwnerAlias(FGuid FieldId) const;

	/**
	 * Get the ID for a field using its label.
	 * @param FieldLabel the field's label.
	 * @return the field's id, or an invalid GUID if not found.
	 */
	FGuid GetFieldId(FName FieldLabel) const;

	/**
	 * Get a field ptr using it's id.
	 */
	TOptional<FRemoteControlField> GetField(FGuid FieldId) const;

	 /**
	  * Get an exposed function using its label.
	  */
	TOptional<FRemoteControlFunction> GetFunction(FName FunctionLabel) const;

	/** 
	 * Get an exposed property using its label.
	 */
	TOptional<FRemoteControlProperty> GetProperty(FName PropertyLabel) const;

	/**
	 * Get an exposed function using its id.
	 */
	TOptional<FRemoteControlFunction> GetFunction(FGuid FunctionId) const;

	/**
	 * Get an exposed property using its id.
	 */
	TOptional<FRemoteControlProperty> GetProperty(FGuid PropertyId) const;

	/**
	 * Rename a field.
	 * @param OldFieldLabel the target field's label.
	 * @param NewFieldLabel the field's new label.
	 */
	void RenameField(FName OldFieldLabel, FName NewFieldLabel);

	/**
	 * Resolve a remote controlled property to its FProperty and owner objects.
	 * @param Alias the target's alias that contains the remote controlled property.
	 * @param PropertyLabel the label of the remote controlled property.
	 * @return The resolved exposed property if found.
	 */
	TOptional<FExposedProperty> ResolveExposedProperty(FName PropertyLabel) const;

	/**
	 * Resolve a remote controlled function to its UFunction and owner objects.
	 * @param Alias the target's alias that contains the remote controlled function.
	 * @param FunctionLabel the label of the remote controlled function.
	 * @return The resolved exposed function if found.
	 */
	TOptional<FExposedFunction> ResolveExposedFunction(FName FunctionLabel) const;

	/**
	 * Unexpose a field from the preset.
	 * @param FieldLabel the field's display name.
	 */
	void Unexpose(FName FieldLabel);

	/**
	 * Unexpose a field from the preset.
	 * @param  FieldId the field's id.
	 */
	void Unexpose(const FGuid& FieldId);

	/**
	 * Create a new target under this preset.
	 * @param TargetObjects The objects to group under a common alias for the target.
	 * @return The new target's alias if successful.
	 * @note A target must be created with at least one object and they must have a common base class.
	 */
	FName CreateTarget(const TArray<UObject*>& TargetObjects);

	/**
	 * Remove a target from the preset.
	 * @param TargetName The target to delete.
	 */
	void DeleteTarget(FName TargetName);

	/**
	 * Rename a target.
	 * @param TargetName The name of the target to rename.
	 * @param NewTargetName The new target's name.
	 */
	void RenameTarget(FName TargetName, FName NewTargetName);

	/** Cache this preset's layout data. */
	void CacheLayoutData();

	/** Resolves exposed property/function bounded objects */
	TArray<UObject*> ResolvedBoundObjects(FName FieldLabel);

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPresetExposedPropertyChanged, URemoteControlPreset* /*Preset*/, const FRemoteControlProperty& /*Property*/);
	FOnPresetExposedPropertyChanged& OnExposedPropertyChanged() { return OnPropertyChangedDelegate; }
	
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPresetPropertyExposed, URemoteControlPreset* /*Preset*/, FName /*ExposedLabel*/);
	FOnPresetPropertyExposed& OnPropertyExposed() { return OnPropertyExposedDelegate; }

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPresetPropertyUnexposed, URemoteControlPreset* /*Preset*/, FName /*ExposedLabel*/);
	FOnPresetPropertyUnexposed& OnPropertyUnexposed() { return OnPropertyUnexposedDelegate; }

	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnPresetFieldRenamed, URemoteControlPreset* /*Preset*/, FName /*OldExposedLabel*/, FName /**NewExposedLabel*/);
	FOnPresetFieldRenamed& OnFieldRenamed() { return OnPresetFieldRenamed; }

	void NotifyExposedPropertyChanged(FName PropertyLabel);

public:
	/** The visual layout for this preset. */
	UPROPERTY()
	FRemoteControlPresetLayout Layout;

	/** This preset's metadata. */
	UPROPERTY()
	TMap<FString, FString> Metadata;

private:
	/** Generate a unique alias for this target. */
	FName GenerateAliasForObjects(const TArray<UObject*>& Objects);
	
	/** Generate a label for a field that is unique preset-wide. */
	FName GenerateUniqueFieldLabel(FName Alias, const FString& BaseName);

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

	//~ Keep track of any property change to notify if one of the exposed property has changed
	void OnObjectPropertyChanged(UObject* Object, struct FPropertyChangedEvent& Event);
	void OnPreObjectPropertyChanged(UObject* Object, const class FEditPropertyChain& PropertyChain);
	void RegisterDelegates();
	void UnregisterDelegates();

	/** Get a field ptr using it's id. */
	FRemoteControlField* GetFieldPtr(FGuid FieldId);

private:
	/** The mappings of alias to targets. */
	UPROPERTY()
	TMap<FName, FRemoteControlTarget> RemoteControlTargets;

	/** The cache for information about an exposed field. */
	UPROPERTY(Transient)
	TMap<FGuid, FRCCachedFieldData> FieldCache;

	/** Delegate triggered when an exposed property value has changed. */
	FOnPresetExposedPropertyChanged OnPropertyChangedDelegate;

	/** Delegate triggered when a new property has been exposed. */
	FOnPresetPropertyExposed OnPropertyExposedDelegate;

	/** Delegate triggered when a property has been unexposed. */
	FOnPresetPropertyUnexposed OnPropertyUnexposedDelegate;

	/** Delegate triggered when a field has been renamed. */
	FOnPresetFieldRenamed OnPresetFieldRenamed;

	struct FPreObjectModifiedCache
	{
		UObject* Object;
		FProperty* Property;
		FProperty* MemberProperty;
	};

	TMap<FGuid, FPreObjectModifiedCache> PreObjectModifiedCache;

	/** Map of Field Name to GUID. */
	UPROPERTY(Transient)
	TMap<FName, FGuid> NameToGuidMap;

	friend FRemoteControlTarget;
	friend FRemoteControlPresetLayout;
};

