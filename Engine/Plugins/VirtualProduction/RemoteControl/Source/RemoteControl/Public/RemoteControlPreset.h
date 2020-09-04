// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "RemoteControlField.h"
#include "UObject/SoftObjectPtr.h"
#include "RemoteControlPreset.generated.h"

/**
 * Represents objects grouped under a single alias that contain exposed functions and properties.
 */
USTRUCT()
struct REMOTECONTROL_API FRemoteControlSection
{
	GENERATED_BODY()
	
public:
	/**
	 * Verifies if the section has the same top level objects as the list to verify.
	 * @param ObjectsToTest The objects to compare.
	 * @return true if the section has the same top level objects.
	 */
	bool HasSameTopLevelObjects(const TArray<UObject*>& ObjectsToTest);

	/**
	 * Exposes a function under this section.
	 * @note HasSameTopLevelObjects is expected to be called before exposing a field.
	 */
	void Expose(FRemoteControlFunction Function);

	/**
	 * Exposes a property under this section.
	 * @note HasSameTopLevelObjects is expected to be called before exposing a field.
	 */
	void Expose(FRemoteControlProperty Property);

	/**
	 * Unexposes a field from this section.
	 * @param TargetFieldId The ID of the field to remove.
	 * @see FindPropertyId
	 */
	void Unexpose(const FGuid& TargetFieldId);

	/**
	 * Finds the ID associated with the property name.
	 * @param PropertyName the property's FName.
	 * @return The GUID associated with the target property if found, or an invalid GUID if not found.
	 */
	FGuid FindPropertyId(FName PropertyName);

	/**
	 * Get an exposed property using the property's ID.
	 * @param FieldId the ID to query.
	 * @return The property if found.
	 */
	TOptional<FRemoteControlProperty> GetProperty(const FGuid& FieldId);

	/**
	 * Resolve the section bindings to return the section's top level objects.
	 * @return The section's top level objects.
	 */
	TArray<UObject*> ResolveSectionObjects() const;

	/**
	 * Adds objects to the section's top level objects, binding them to the section's alias.
	 * @param ObjectsToBind The objects to bind.
	 */
	void AddBindings(const TArray<UObject*>& ObjectsToBind);
private:

	// Remove a field from the specified fields array.
	template <typename Type> 
	void RemoveField(TSet<Type>& Fields, const FGuid& TargetFieldId)
	{
		Fields.RemoveByHash(GetTypeHash(TargetFieldId), TargetFieldId);
	}

public:
	/**
	 * The common class of the section's top level objects.
	 */
	UPROPERTY()
	UClass* SectionClass = nullptr;

	/**
	 * The section's exposed functions.
	 */
	UPROPERTY()
	TSet<FRemoteControlFunction> ExposedFunctions;

	/**
	 * The section's exposed properties.
	 */
	UPROPERTY()
	TSet<FRemoteControlProperty> ExposedProperties;

	/**
	 * The alias that represents the section's top level objects.
	 */
	UPROPERTY()
	FString Alias;
private:
	/**
	 * The objects bound under the section's alias.
	 */
	UPROPERTY()
	TArray<FSoftObjectPath> Bindings;
};

/**
 * Holds sections that contain exposed functions and properties.
 */
UCLASS(BlueprintType)
class REMOTECONTROL_API URemoteControlPreset : public UObject
{
public:
	GENERATED_BODY()
		                                          
	/**
	 * Get this preset's sections.
	 */
	TMap<FString, FRemoteControlSection>& GetRemoteControlSections() { return RemoteControlSections; }

	/**
	 * Get this preset's sections.
	 */
	const TMap<FString, FRemoteControlSection>& GetRemoteControlSections() const { return RemoteControlSections; }

	/**
	 * Check if a section can be created with the given objects.
	 * @param SectionObjects The top level objects of the section.
	 * @return Whether or not the section can be created.
	 */
	bool CanCreateSection(const TArray<UObject*>& SectionObjects);

	/**
	 * Create a new section under this preset.
	 * @param SectionObjects The objects to group under a common alias for the section.
	 * @return The created section.
	 * @note A section must be created with at least one object and they must have a common base class.
	 */
	FRemoteControlSection& CreateSection(const TArray<UObject*>& SectionObjects);

	/**
	 * Remove a section from the preset.
	 * @param SectionName The section to delete.
	 */
	void DeleteSection(const FString& SectionName);

	/**
	 * Rename a section.
	 * @param SectionName The name of the section to rename.
	 * @param NewSectionName The new section's name.
	 */
	void RenameSection(const FString& SectionName, const FString& NewSectionName);
private:
	/** Create an alias for an object list. */
	FString GenerateAliasForObjects(const TArray<UObject*>& Objects);

	/** Create a unique name. */
	FString MakeUniqueName(const FString& InBase);
private:
	/** The mappings of alias to exposed sections. */
	UPROPERTY()
	TMap<FString, FRemoteControlSection> RemoteControlSections;
};
