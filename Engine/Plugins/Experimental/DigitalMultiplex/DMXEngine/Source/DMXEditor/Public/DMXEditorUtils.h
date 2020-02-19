// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "Library/DMXEntity.h"

class UDMXLibrary;
class UDMXEntityFixtureType;
class UDMXEntityFader;

class DMXEDITOR_API FDMXEditorUtils
{
public:
	/**
	 * Utility to separate a name from an index at the end.
	 * @param InString	The string to be separated.
	 * @param OutName	The string without an index at the end. White spaces and '_' are also removed.
	 * @param OutIndex	Index that was separated from the name. If there was none, it's zero.
	 *					Check the return value to know if there was an index on InString.
	 * @return True if there was an index on InString.
	 */
	static bool GetNameAndIndexFromString(const FString& InString, FString& OutName, int32& OutIndex);

	/**
	 * Generates a unique name given a base one and a list of existing ones, by appending an index to
	 * existing names. If InBaseName is an empty String, it returns "Default name".
	 */
	static FString GenerateUniqueNameFromExisting(const TSet<FString>& InExistingNames, const FString& InBaseName);

	/**
	 * Creates an unique name for an Entity from a specific type, using the type name as base.
	 * @param InLibrary		The DMXLibrary object the entity will belong to.
	 * @param InEntityClass	The class of the Entity, to check the name against others from same type.
	 * @param InBaseName	Optional base name to use instead of the type name.
	 * @return Unique name for an Entity amongst others from the same type.
	 */
	static FString FindUniqueEntityName(const UDMXLibrary* InLibrary, TSubclassOf<UDMXEntity> InEntityClass, const FString& InBaseName = TEXT(""));

	/**
	 * Set unique names for Fixture Types' Modes, Functions and Sub Functions when they have just been created.
	 * We simply rename the Modes/Functions with no name. The user can't set a blank name afterwards, so it's
	 * a good way to know which are the new ones.
	 */
	static void SetNewFixtureFunctionsNames(UDMXEntityFixtureType* InFixtureType);

	/**
	 * Creates a named Entity from the specified type and add it to the DMXLibrary.
	 * @param InLibrary			The DMXLibrary object the entity will belong to.
	 * @param NewEntityName		A name for the new Entity.
	 * @param NewEntityClass	Type of the new Entity.
	 * @param OutNewEntity		Returns the created entity.
	 * @return True if the creation was successful.
	 */
	static bool AddEntity(UDMXLibrary* InLibrary, const FString& NewEntityName, TSubclassOf<UDMXEntity> NewEntityClass, UDMXEntity** OutNewEntity = nullptr);

	/**
	 * Validates an Entity name, also checking for uniqueness among others of the same type.
	 * @param NewEntityName		The name to validate.
	 * @param InLibrary			The DMXLibrary object to check for name uniqueness.
	 * @param InEntityClass		The type to check other Entities' names
	 * @param OutReason			If false is returned, contains a text with the reason for it.
	 * @return True if the name would be a valid one.
	 */
	static bool ValidateEntityName(const FString& NewEntityName, const UDMXLibrary* InLibrary, UClass* InEntityClass, FText& OutReason);

	/**
	 * Creates new fader template.
	 * @param InLibrary			The DMXLibrary object to check for name uniqueness.
	 * @return New transient fader template object.
	 */
	static UDMXEntityFader* CreateFaderTemplate(const UDMXLibrary* InLibrary);

	/**  Renames an Entity */
	static void RenameEntity(UDMXLibrary* InLibrary, UDMXEntity* InEntity, const FString& NewName);

	/**  Checks if the Entity is being referenced by other objects. */
	static bool IsEntityUsed(const UDMXLibrary* InLibrary, const UDMXEntity* InEntity);

	/**  Removes the entities and fixes references to it. */
	static void RemoveEntities(UDMXLibrary* InLibrary, const TArray<UDMXEntity*>&& InEntities);

	/**  Copies Entities to the operating system's clipboard. */
	static void CopyEntities(const TArray<UDMXEntity*>&& EntitiesToCopy);

	/**  Determines whether the current contents of the clipboard contain paste-able DMX Entity information */
	static bool CanPasteEntities();

	/**
	 * Gets the copied DMX Entities from the clipboard without attempting to paste/apply them in any way
	 * @param OutNewObjectMap			Contains the name->instance object mapping of the copied DMX Entities
	 */
	static void GetEntitiesFromClipboard(TArray<UDMXEntity*>& OutNewObjects);

	/**
	 * Compares the property values of two Fixture Types, including properties in arrays,
	 * and returns true if they are almost all the same.
	 * Name, ID and Parent Library are ignored.
	 */
	static bool AreFixtureTypesIdentical(const UDMXEntityFixtureType* A, const UDMXEntityFixtureType* B);

	/**  Returns the Entity class type name (e.g: Fixture Type for UDMXEntityFixtureType) in singular or plural */
	static FText GetEntityTypeNameText(TSubclassOf<UDMXEntity> EntityClass, bool bPlural = false);

	// can't instantiate this class
	FDMXEditorUtils() = delete;
};
