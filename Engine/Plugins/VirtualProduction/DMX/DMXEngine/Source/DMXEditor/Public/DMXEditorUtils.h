// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "Library/DMXEntity.h"

class UDMXLibrary;
class UDMXEntityFixtureType;
class UDMXEntityFader;
class UDMXEntityFixturePatch;


class DMXEDITOR_API FDMXEditorUtils
{
public:
	typedef TArray<UDMXEntityFixturePatch*> FUnassignedPatchesArray;
		
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
	 * Set unique names for Fixture Types' Modes and Functions when they have just been created.
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

	/**  Renames an Entity */
	static void RenameEntity(UDMXLibrary* InLibrary, UDMXEntity* InEntity, const FString& NewName);

	/**  Checks if the Entity is being referenced by other objects. */
	static bool IsEntityUsed(const UDMXLibrary* InLibrary, const UDMXEntity* InEntity);

	/**  Removes the entities and fixes references to it. */
	static void RemoveEntities(UDMXLibrary* InLibrary, const TArray<UDMXEntity*>& InEntities);

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

	
	
// Auto Assign:
	
	/**
	 * Updates starting address for the patch, if it has bAutoAssignAddress set and only if it can be assigned
	 * to a universe in AllowedUniverses.
	 *
	 * If no universe can be assigned, then this patch is left unmodified.
	 *
	 * @return Whether Patch was auto assigned.
	 */
	static bool TryAutoAssignToUniverses(UDMXEntityFixturePatch* Patch, const TSet<int32>& AllowedUniverses);
	
	/**
	 * Updates Addresses for Fixture Patches that use specified Parent fixture type and have bAutoAssignAddress set. 
	 *
	 * @param ChangedParentFixtureType	The parent fixture type of the patches that want their channels to be auto assigned
	 */
	static void AutoAssignedAddresses(UDMXEntityFixtureType* ChangedParentFixtureType);

	/**
	 * Updates starting addresses for fixture patches that have bAutoAssignAddress set, ignores others.
	 * Note, patches all have to reside in the same library.
	 *
	 * The caller is responsible to call Modify on the patches and register undo/redo.
	 *
	 * If bCanChangePatchUniverses = true, this function will assign patches that do not fit into the existing universes to
	 * the next universe. MinimumAddress is ignored for the new universe, i.e. we start placing remaining patches at position 1.
	 *
	 * @param ChangedFixturePatches		The patches that want their channels to be auto assigned
	 * @param MinimumAddress			All patches must be placed after this address
	 * @param bCanChangePatchUniverses	Whether we are allowed to move a patch to another universe if the assigned universe has no space.
	 *
	 * @result Patches that were not assigned.
	 */
	static FUnassignedPatchesArray AutoAssignedAddresses(const TArray<UDMXEntityFixturePatch*>& ChangedFixturePatches, int32 MinimumAddress = 1, bool bCanChangePatchUniverses = true);
	
	/**
	 * Creates a unique color for all patches that use the default color FLinearColor(1.0f, 0.0f, 1.0f)
	 *
	 * @param Library				The library the patches resides in.
	 */
	static void UpdatePatchColors(UDMXLibrary* Library);

	/**
	 * Retrieve all assets for a given class via the asset registry. Will load into memory if needed.
	 *
	 * @param Class					The class to lookup.
	 * @param OutObjects			All found objects.
	 * 
	 */
	static void GetAllAssetsOfClass(UClass* Class, TArray<UObject*>& OutObjects);

	/**
	 * Locate universe conflicts between libraries
	 *
	 * @param Library					The library to be tested.
	 * @param OutConflictMessage		Message containing found conflicts
	 * @return							True if there is at least one conflict found.
	 */
	static bool DoesLibraryHaveUniverseConflicts(UDMXLibrary* Library, FText& OutInputPortConflictMessage, FText& OutOutputPortConflictMessage);

	/** Zeros memory in all active DMX buffers of all protocols */
	static void ClearAllDMXPortBuffers();

	/** Clears cached data fixture patches received */
	static void ClearFixturePatchCachedData();

	// can't instantiate this class
	FDMXEditorUtils() = delete;
};
