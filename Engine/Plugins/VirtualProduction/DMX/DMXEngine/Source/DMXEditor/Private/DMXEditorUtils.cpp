// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXEditorUtils.h"

#include "DMXEditorLog.h"
#include "DMXRuntimeUtils.h"
#include "DMXSubsystem.h"
#include "Interfaces/IDMXProtocol.h"
#include "IO/DMXInputPort.h"
#include "IO/DMXOutputPort.h"
#include "IO/DMXPortManager.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXEntityFixturePatch.h"

#include "AssetRegistryModule.h"
#include "Factories.h"
#include "ScopedTransaction.h"
#include "UnrealExporter.h"
#include "Dialogs/Dialogs.h"
#include "Exporters/Exporter.h"
#include "HAL/PlatformApplicationMisc.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "FDMXEditorUtils"
// In blueprints name verification, it is said that '.' is known for causing problems
#define DMX_INVALID_NAME_CHARACTERS TEXT(".")

// Text object factory for pasting DMX Entities
struct FDMXEntityObjectTextFactory : public FCustomizableTextObjectFactory
{
	/** Entities instantiated */
	TArray<UDMXEntity*> NewEntities;

	/** Constructs a new object factory from the given text buffer */
	static TSharedRef<FDMXEntityObjectTextFactory> Get(const FString& InTextBuffer)
	{
		// Construct a new instance
		TSharedPtr<FDMXEntityObjectTextFactory> FactoryPtr = MakeShareable(new FDMXEntityObjectTextFactory());
		check(FactoryPtr.IsValid());

		// Create new objects if we're allowed to
		if (FactoryPtr->CanCreateObjectsFromText(InTextBuffer))
		{
			EObjectFlags ObjectFlags = RF_Transactional;

			// Use the transient package initially for creating the objects, since the variable name is used when copying
			FactoryPtr->ProcessBuffer(GetTransientPackage(), ObjectFlags, InTextBuffer);
		}

		return FactoryPtr.ToSharedRef();
	}

protected:
	/** Constructor; protected to only allow this class to instance itself */
	FDMXEntityObjectTextFactory()
		: FCustomizableTextObjectFactory(GWarn)
	{
	}

	//~ Begin FCustomizableTextObjectFactory implementation
	virtual bool CanCreateClass(UClass* ObjectClass, bool& bOmitSubObjs) const override
	{
		// Allow DMX Entity types to be created
		return ObjectClass->IsChildOf(UDMXEntity::StaticClass());
	}

	virtual void ProcessConstructedObject(UObject* NewObject) override
	{
		check(NewObject);

		if (UDMXEntity* NewEntity = Cast<UDMXEntity>(NewObject))
		{
			// If this is a Fixture Type and the first object was a Patch, we
			// don't add the Type to the array. SDMXEntityList will deal with it
			// from the reference on the Patch(es)
			if (NewEntities.Num() > 0 && NewEntities[0]->GetClass()->IsChildOf<UDMXEntityFixturePatch>()
				&& NewEntity->GetClass()->IsChildOf<UDMXEntityFixtureType>())
			{
				return;
			}

			NewEntities.Add(NewEntity);
		}
	}
	//~ End FCustomizableTextObjectFactory implementation
};


FString FDMXEditorUtils::GenerateUniqueNameFromExisting(const TSet<FString>& InExistingNames, const FString& InBaseName)
{
	if (!InBaseName.IsEmpty() && !InExistingNames.Contains(InBaseName))
	{
		return InBaseName;
	}

	FString FinalName;
	FString BaseName;

	int32 Index = 0;
	if (InBaseName.IsEmpty())
	{
		BaseName = TEXT("Default name");
	}
	else
	{
		// If there's an index at the end of the name, start from there
		FDMXRuntimeUtils::GetNameAndIndexFromString(InBaseName, BaseName, Index);
	}

	int32 Count = (Index == 0) ? 1 : Index;
	FinalName = BaseName;
	// Add Count to the BaseName, increasing Count, until it's a non-existent name
	do
	{
		// Calculate the number of digits in the number, adding 2 (1 extra to correctly count digits, another to account for the '_' that will be added to the name
		int32 CountLength = Count > 0 ? (int32)FGenericPlatformMath::LogX(10.0f, Count) + 2 : 2;

		// If the length of the final string will be too long, cut off the end so we can fit the number
		if (CountLength + BaseName.Len() >= NAME_SIZE)
		{
			BaseName = BaseName.Left(NAME_SIZE - CountLength - 1);
		}

		FinalName = FString::Printf(TEXT("%s_%d"), *BaseName, Count);
		++Count;
	} while (InExistingNames.Contains(FinalName));

	return FinalName;
}

FString FDMXEditorUtils::FindUniqueEntityName(const UDMXLibrary* InLibrary, TSubclassOf<UDMXEntity> InEntityClass, const FString& InBaseName /*= TEXT("")*/)
{
	check(InLibrary != nullptr);

	// Get existing names for the current entity type
	TSet<FString> EntityNames;
	InLibrary->ForEachEntityOfType(InEntityClass, [&EntityNames](UDMXEntity* Entity)
		{
			EntityNames.Add(Entity->GetDisplayName());
		});

	FString BaseName = InBaseName;

	// If no base name was set, use the entity class name as base
	if (BaseName.IsEmpty())
	{
		BaseName = InEntityClass->GetDisplayNameText().ToString();
	}

	return GenerateUniqueNameFromExisting(EntityNames, BaseName);
}

void FDMXEditorUtils::SetNewFixtureFunctionsNames(UDMXEntityFixtureType* InFixtureType)
{
	check(InFixtureType != nullptr);

	// We'll only populate this Set if we find an item with no name.
	// Otherwise we can save some for loops.
	TSet<FString> ModesNames;

	// Iterate over all of the Fixture's Modes and Functions creating names for the
	// ones with a blank name.
	for (FDMXFixtureMode& Mode : InFixtureType->Modes)
	{
		// Do we need to name this mode?
		if (Mode.ModeName.IsEmpty())
		{
			// Cache existing names only once, when needed.
			if (ModesNames.Num() == 0)
			{
				// Cache the existing modes' names
				for (FDMXFixtureMode& NamedMode : InFixtureType->Modes)
				{
					if (!NamedMode.ModeName.IsEmpty())
					{
						ModesNames.Add(NamedMode.ModeName);
					}
				}
			}

			Mode.ModeName = GenerateUniqueNameFromExisting(ModesNames, TEXT("Mode"));
			ModesNames.Add(Mode.ModeName);
		}

		// Name this mode's functions
		TSet<FString> FunctionsNames;
		for (FDMXFixtureFunction& Function : Mode.Functions)
		{
			if (Function.FunctionName.IsEmpty())
			{
				// Cache existing names only once, when needed.
				if (FunctionsNames.Num() == 0)
				{
					for (FDMXFixtureFunction& NamedFunction : Mode.Functions)
					{
						if (!NamedFunction.FunctionName.IsEmpty())
						{
							FunctionsNames.Add(NamedFunction.FunctionName);
						}
					}
				}

				Function.FunctionName = GenerateUniqueNameFromExisting(FunctionsNames, TEXT("Function"));
				FunctionsNames.Add(Function.FunctionName);
			}
		}
	}
}

bool FDMXEditorUtils::AddEntity(UDMXLibrary* InLibrary, const FString& NewEntityName, TSubclassOf<UDMXEntity> NewEntityClass, UDMXEntity** OutNewEntity /*= nullptr*/)
{
	// Don't allow entities with empty names
	if (NewEntityName.IsEmpty())
	{
		return false;
	}

	// Mark library as pending save and store current state for undo
	const FScopedTransaction NewEntityTransaction(LOCTEXT("NewEntityTransaction", "Add new Entity to DMX Library"));
	InLibrary->Modify();

	// Create new entity
	*OutNewEntity = InLibrary->GetOrCreateEntityObject(NewEntityName, NewEntityClass);

	return true;
}

bool FDMXEditorUtils::ValidateEntityName(const FString& NewEntityName, const UDMXLibrary* InLibrary, UClass* InEntityClass, FText& OutReason)
{
	if (NewEntityName.Len() >= NAME_SIZE)
	{
		OutReason = LOCTEXT("NameTooLong", "The name is too long");
		return false;
	}

	if (NewEntityName.TrimStartAndEnd().IsEmpty())
	{
		OutReason = LOCTEXT("NameEmpty", "The name can't be blank!");
		return false;
	}

	for (const TCHAR& Character : DMX_INVALID_NAME_CHARACTERS)
	{
		if (NewEntityName.Contains(&Character))
		{
			OutReason = FText::Format(LOCTEXT("NameWithInvalidCharacters", "Name can not contain: {0}"),
				FText::FromString(&Character));
			return false;
		}
	}

	// Check against existing names for the current entity type
	bool bNameIsUsed = false;
	InLibrary->ForEachEntityOfTypeWithBreak(InEntityClass, [&bNameIsUsed, &NewEntityName](UDMXEntity* Entity)
		{
			if (Entity->GetDisplayName() == NewEntityName)
			{
				bNameIsUsed = true;
				return false; // Break the loop
			}
			return true; // Keep checking Entities' names
		});

	if (bNameIsUsed)
	{
		OutReason = LOCTEXT("ExistingEntityName", "Name already exists");
		return false;
	}
	else
	{
		OutReason = FText::GetEmpty();
		return true;
	}
}

void FDMXEditorUtils::RenameEntity(UDMXLibrary* InLibrary, UDMXEntity* InEntity, const FString& NewName)
{
	if (InEntity == nullptr)
	{
		return;
	}

	if (!NewName.IsEmpty() && !NewName.Equals(InEntity->GetDisplayName()))
	{
		const FScopedTransaction Transaction(LOCTEXT("RenameEntity", "Rename Entity"));
		InEntity->Modify();

		// Update the name
		InEntity->SetName(NewName);
	}
}

bool FDMXEditorUtils::IsEntityUsed(const UDMXLibrary* InLibrary, const UDMXEntity* InEntity)
{
	if (InLibrary != nullptr && InEntity != nullptr)
	{
		if (const UDMXEntityFixtureType* EntityAsFixtureType = Cast<UDMXEntityFixtureType>(InEntity))
		{
			bool bIsUsed = false;
			InLibrary->ForEachEntityOfTypeWithBreak<UDMXEntityFixturePatch>([&](UDMXEntityFixturePatch* Patch)
				{
					if (Patch->GetFixtureType() == InEntity)
					{
						bIsUsed = true;
						return false;
					}
					return true;
				});

			return bIsUsed;
		}
		else
		{
			return false;
		}
	}

	return false;
}

void FDMXEditorUtils::RemoveEntities(UDMXLibrary* InLibrary, const TArray<UDMXEntity*>& InEntities)
{
	if (InLibrary)
	{
		TArray<UDMXEntity*> EntitiesInUse;
		for (UDMXEntity* Entity : InEntities)
		{
			if (FDMXEditorUtils::IsEntityUsed(InLibrary, Entity))
			{
				EntitiesInUse.Add(Entity);
			}
		}

		// Confirm deletion of Entities in use, if any
		if (EntitiesInUse.Num() > 0)
		{
			FText ConfirmDelete;

			// Confirmation text for a single entity in use
			if (EntitiesInUse.Num() == 1)
			{
				ConfirmDelete = FText::Format(LOCTEXT("ConfirmDeleteEntityInUse", "Entity \"{0}\" is in use! Do you really want to delete it?"),
					FText::FromString(EntitiesInUse[0]->Name));
			}
			// Confirmation text for when all of the selected entities are in use
			else if (EntitiesInUse.Num() == InEntities.Num())
			{
				ConfirmDelete = LOCTEXT("ConfirmDeleteAllEntitiesInUse", "All selected entities are in use! Do you really want to delete them?");
			}
			// Confirmation text for multiple entities, but not so much that would make the dialog huge
			else if (EntitiesInUse.Num() > 1 && EntitiesInUse.Num() <= 10)
			{
				FString EntitiesNames;
				for (UDMXEntity* Entity : EntitiesInUse)
				{
					EntitiesNames += TEXT("\t") + Entity->GetDisplayName() + TEXT("\n");
				}

				ConfirmDelete = FText::Format(LOCTEXT("ConfirmDeleteSomeEntitiesInUse", "The Entities below are in use!\n{0}\nDo you really want to delete them?"),
					FText::FromString(EntitiesNames));
			}
			// Confirmation text for several entities. Displaying each of their names would make a huge dialog
			else
			{
				ConfirmDelete = FText::Format(LOCTEXT("ConfirmDeleteManyEntitiesInUse", "{0} of the selected entities are in use!\nDo you really want to delete them?"),
					FText::AsNumber(EntitiesInUse.Num()));
			}

			// Warn the user that this may result in data loss
			FSuppressableWarningDialog::FSetupInfo Info(ConfirmDelete, LOCTEXT("DeleteEntities", "Delete Entities"), "DeleteEntitiesInUse_Warning");
			Info.ConfirmText = LOCTEXT("DeleteEntities_Yes", "Yes");
			Info.CancelText = LOCTEXT("DeleteEntities_No", "No");

			FSuppressableWarningDialog DeleteEntitiesInUse(Info);
			if (DeleteEntitiesInUse.ShowModal() == FSuppressableWarningDialog::Cancel)
			{
				return;
			}
		}

		const FScopedTransaction Transaction(InEntities.Num() > 1 ? LOCTEXT("RemoveEntities", "Remove Entities") : LOCTEXT("RemoveEntity", "Remove Entity"));

		for (UDMXEntity* EntityToDelete : InEntities)
		{
			// Fix references to this Entity
			if (UDMXEntityFixtureType* AsFixtureType = Cast<UDMXEntityFixtureType>(EntityToDelete))
			{
				// Find Fixture Patches using this Fixture Type and null their templates
				InLibrary->ForEachEntityOfType<UDMXEntityFixturePatch>([&AsFixtureType](UDMXEntityFixturePatch* Patch)
					{
						Patch->SetFixtureType(nullptr);
					});
			}

			InLibrary->Modify();
			EntityToDelete->Modify(); // Take a snapshot of the entity before setting its ParentLibrary to null
			InLibrary->RemoveEntity(EntityToDelete);
		}
	}
}

void FDMXEditorUtils::CopyEntities(const TArray<UDMXEntity*>&& EntitiesToCopy)
{
	// Clear the mark state for saving.
	UnMarkAllObjects(EObjectMark(OBJECTMARK_TagExp | OBJECTMARK_TagImp));

	const FExportObjectInnerContext Context;
	FStringOutputDevice Archive;

	// Stores duplicates of the Fixture Type Templates because they can't be parsed being children of
	// a DMX Library asset since they're private objects.
	TMap<FName, UDMXEntityFixtureType*> CopiedPatchTemplates;

	// Export the component object(s) to text for copying
	for (UDMXEntity* Entity : EntitiesToCopy)
	{
		check(Entity && Entity->GetParentLibrary());

		// Fixture Patches require copying their template because it's a reference to a private object
		if (UDMXEntityFixturePatch* AsPatch = Cast<UDMXEntityFixturePatch>(Entity))
		{
			if (UDMXEntityFixtureType* FixtureType = AsPatch->GetFixtureType())
			{
				bool bExportedTemplate = false;

				// Try to get a cached duplicate of the template
				UDMXEntityFixtureType* DuplicateFixtureType;
				if (UDMXEntityFixtureType** CachedTemplate = CopiedPatchTemplates.Find(FixtureType->GetFName()))
				{
					DuplicateFixtureType = *CachedTemplate;
					bExportedTemplate = true;
				}
				else
				{
					// Copy the template to the transient package to make the Patch reference the copy
					FObjectDuplicationParameters DuplicationParams(FixtureType, GetTransientPackage());
					DuplicationParams.DestName = FixtureType->GetFName();

					DuplicateFixtureType = CastChecked<UDMXEntityFixtureType>(StaticDuplicateObjectEx(DuplicationParams));
					// Keep same entity ID to find the original Template when pasting
					DuplicateFixtureType->ReplicateID(FixtureType);

					// Cache this copy so we don't copy the same template over and over for several Patches
					CopiedPatchTemplates.Add(DuplicateFixtureType->GetFName(), DuplicateFixtureType);
				}

				// We'll temporarily change the ParentFixtureTypeTemplate of the Patch to copy it
				// with a reference to the duplicate Fixture Type
				UDMXEntityFixtureType* OriginalTemplate = FixtureType;
				FixtureType = DuplicateFixtureType;
				// Export the Patch referencing the duplicate template
				UExporter::ExportToOutputDevice(&Context, AsPatch, nullptr, Archive, TEXT("copy"), 0, PPF_ExportsNotFullyQualified | PPF_Copy | PPF_Delimited, false, GetTransientPackage());
				if (!bExportedTemplate)
				{
					// Export the template after the Patch, to make interpretation easier when pasting it back
					UExporter::ExportToOutputDevice(&Context, DuplicateFixtureType, nullptr, Archive, TEXT("copy"), 4, PPF_ExportsNotFullyQualified | PPF_Copy | PPF_Delimited, false, GetTransientPackage());
				}
				// Revert the patch to it's original, private template
				FixtureType = OriginalTemplate;
			}
			else // Template is null
			{
				// Export the entity object to the given string
				UExporter::ExportToOutputDevice(&Context, AsPatch, nullptr, Archive, TEXT("copy"), 0, PPF_ExportsNotFullyQualified | PPF_Copy | PPF_Delimited, false, GetTransientPackage());
			}
		}
		else
		{
			// Export the entity object to the given string
			UExporter::ExportToOutputDevice(&Context, Entity, nullptr, Archive, TEXT("copy"), 0, PPF_ExportsNotFullyQualified | PPF_Copy | PPF_Delimited, false, GetTransientPackage());
		}
	}

	// Speed up the deletion of the copies. We don't need them anymore
	for (const TPair<FName, UDMXEntityFixtureType*>& CopiedTemplate : CopiedPatchTemplates)
	{
		CopiedTemplate.Value->ConditionalBeginDestroy();
	}

	// Copy text to clipboard
	FString ExportedText = Archive;
	FPlatformApplicationMisc::ClipboardCopy(*ExportedText);
}

bool FDMXEditorUtils::CanPasteEntities()
{
	FString ClipboardContent;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);

	// Obtain the entity object text factory for the clipboard content and return whether or not we can use it
	TSharedRef<FDMXEntityObjectTextFactory> Factory = FDMXEntityObjectTextFactory::Get(ClipboardContent);
	return Factory->NewEntities.Num() > 0;
}

void FDMXEditorUtils::GetEntitiesFromClipboard(TArray<UDMXEntity*>& OutNewObjects)
{
	// Get the text from the clipboard
	FString TextToImport;
	FPlatformApplicationMisc::ClipboardPaste(TextToImport);

	// Get a new component object factory for the clipboard content
	TSharedRef<FDMXEntityObjectTextFactory> Factory = FDMXEntityObjectTextFactory::Get(TextToImport);

	// Return the created component mappings
	OutNewObjects = MoveTemp(Factory->NewEntities);
}

bool FDMXEditorUtils::AreFixtureTypesIdentical(const UDMXEntityFixtureType* A, const UDMXEntityFixtureType* B)
{
	if (A == B)
	{
		return true;
	}
	if (A == nullptr || B == nullptr)
	{
		return false;
	}
	if (A->GetClass() != B->GetClass())
	{
		return false;
	}

	// Compare each UProperty in the Fixtures
	const UStruct* Struct = UDMXEntityFixtureType::StaticClass();
	TPropertyValueIterator<const FProperty> ItA(Struct, A);
	TPropertyValueIterator<const FProperty> ItB(Struct, B);

	static const FName NAME_ParentLibrary = TEXT("ParentLibrary");
	static const FName NAME_Id = TEXT("Id");

	for (; ItA && ItB; ++ItA, ++ItB)
	{
		const FProperty* PropertyA = ItA->Key;
		const FProperty* PropertyB = ItB->Key;

		if (PropertyA == nullptr || PropertyB == nullptr)
		{
			return false;
		}

		// Properties must be in the exact same order on both Fixtures. Otherwise, it means we have
		// different properties being compared due to differences in array sizes.
		if (!PropertyA->SameType(PropertyB))
		{
			return false;
		}

		// Name and Id don't have to be identical
		if (PropertyA->GetFName() == GET_MEMBER_NAME_CHECKED(UDMXEntity, Name)
			|| PropertyA->GetFName() == NAME_ParentLibrary) // Can't GET_MEMBER_NAME... with private properties
		{
			continue;
		}

		if (PropertyA->GetFName() == NAME_Id)
		{
			// Skip all properties from GUID struct
			for (int32 PropertyCount = 0; PropertyCount < 4; ++PropertyCount)
			{
				++ItA;
				++ItB;
			}
			continue;
		}

		const void* ValueA = ItA->Value;
		const void* ValueB = ItB->Value;

		if (!PropertyA->Identical(ValueA, ValueB))
		{
			return false;
		}
	}

	// If one of the Property Iterators is still valid, one of the Fixtures had
	// less properties due to an array size difference, which means the Fixtures are different.
	if (ItA || ItB)
	{
		return false;
	}

	return true;
}

FText FDMXEditorUtils::GetEntityTypeNameText(TSubclassOf<UDMXEntity> EntityClass, bool bPlural /*= false*/)
{
	if (EntityClass->IsChildOf(UDMXEntityFixtureType::StaticClass()))
	{
		return FText::Format(
			LOCTEXT("EntityTypeName_FixtureType", "Fixture {0}|plural(one=Type, other=Types)"),
			bPlural ? 2 : 1
		);
	}
	else if (EntityClass->IsChildOf(UDMXEntityFixturePatch::StaticClass()))
	{
		return FText::Format(
			LOCTEXT("EntityTypeName_FixturePatch", "Fixture {0}|plural(one=Patch, other=Patches)"),
			bPlural ? 2 : 1
		);
	}
	else
	{
		return FText::Format(
			LOCTEXT("EntityTypeName_NotImplemented", "{0}|plural(one=Entity, other=Entities)"),
			bPlural ? 2 : 1
		);
	}
}

bool FDMXEditorUtils::TryAutoAssignToUniverses(UDMXEntityFixturePatch* Patch, const TSet<int32>& AllowedUniverses)
{
	check(Patch->IsAutoAssignAddress());
	const int32 UniverseToRestore = Patch->GetUniverseID();
	const int32 AutoAddressToRestore = Patch->GetAutoStartingAddress();
	
	for(auto UniverseIt = AllowedUniverses.CreateConstIterator(); UniverseIt; ++UniverseIt)
	{
		// Don't auto assign to a universe smaller than the initial one
		if (Patch->GetUniverseID() > *UniverseIt)
		{
			continue;
		}

		Patch->SetUniverseID(*UniverseIt);
		const FUnassignedPatchesArray UnassignedPatches = AutoAssignedAddresses({ Patch }, 1, false);
		
		const bool bWasPatchAssignedToUniverse = UnassignedPatches.Num() == 0;
		if(bWasPatchAssignedToUniverse)
		{
			return true;
		}
	}

	Patch->SetUniverseID(UniverseToRestore);
	Patch->SetAutoStartingAddress(AutoAddressToRestore);
	return false;
}

void FDMXEditorUtils::AutoAssignedAddresses(UDMXEntityFixtureType* ChangedParentFixtureType)
{
	if (ChangedParentFixtureType)
	{
		UDMXLibrary* Library = ChangedParentFixtureType->GetParentLibrary();
		check(Library);

		TArray<UDMXEntityFixturePatch*> FixturePatches;
		Library->ForEachEntityOfType<UDMXEntityFixturePatch>([&FixturePatches, ChangedParentFixtureType](UDMXEntityFixturePatch* Patch)
			{
				if (Patch->GetFixtureType() == ChangedParentFixtureType)
				{
					FixturePatches.Add(Patch);
				}
			});

		AutoAssignedAddresses(FixturePatches);
	}
}

FDMXEditorUtils::FUnassignedPatchesArray FDMXEditorUtils::AutoAssignedAddresses(
	const TArray<UDMXEntityFixturePatch*>& ChangedFixturePatches,
	int32 MinimumAddress,
	bool bCanChangePatchUniverses)
{
	struct Local
	{
		static void AssignAddressesInUniverse(
			TArray<UDMXEntityFixturePatch*>& SortedPatchesWithSetAddress,
			TArray<UDMXEntityFixturePatch*>& PatchesToAssign, 
			int32 MinimumAddress,
			const int32 UniverseToAssignTo,
			TArray<UDMXEntityFixturePatch*>& UnassignedPatches)
		{
			if(PatchesToAssign.Num() == 0)
			{
				return;
			}
			
			int32 IndexOfFirstPatchWithMinAddress =
				SortedPatchesWithSetAddress.IndexOfByPredicate([MinimumAddress](UDMXEntityFixturePatch* Other)
				{
					const int32 ChannelEnd = Other->GetStartingChannel() + Other->GetChannelSpan();
					return ChannelEnd >= MinimumAddress;
				});
			
			for(UDMXEntityFixturePatch* ToAssign : PatchesToAssign)
			{
				const bool bIsUniverseEmpty = IndexOfFirstPatchWithMinAddress == INDEX_NONE;
				if (bIsUniverseEmpty)
				{
					if(HasEnoughSpaceToUniverseEnd(ToAssign, MinimumAddress))
					{
						IndexOfFirstPatchWithMinAddress = 0;
						AssignPatchTo(ToAssign, MinimumAddress, UniverseToAssignTo);
						SortedPatchesWithSetAddress.Add(ToAssign);
					}
					else
					{
						UnassignedPatches.Add(ToAssign);
					}
					continue;
				}

				const bool bFoundGap = FillIntoFirstGap(ToAssign, UniverseToAssignTo, MinimumAddress, IndexOfFirstPatchWithMinAddress, SortedPatchesWithSetAddress);
				if(!bFoundGap)
				{
					UDMXEntityFixturePatch* LastPatchInUniverse = SortedPatchesWithSetAddress[SortedPatchesWithSetAddress.Num() - 1];
					if(HasEnoughSpaceToUniverseEnd(ToAssign, GetEndAddressOf(LastPatchInUniverse) + 1))
					{
						AssignPatchTo(ToAssign, GetEndAddressOf(LastPatchInUniverse) + 1, UniverseToAssignTo);
						SortedPatchesWithSetAddress.Add(ToAssign);
					}
					else
					{
						UnassignedPatches.Add(ToAssign);
					}
				}
			}
		}
		
		static bool HasEnoughSpaceToUniverseEnd(UDMXEntityFixturePatch* Patch, int32 AtAddress)
		{
			// + 1 is needed otherwise we're off by one, e.g. when AtAddress=DMX_UNIVERSE_SIZE and ChannelSpan=1
			return (DMX_UNIVERSE_SIZE + 1) - AtAddress >= Patch->GetChannelSpan();
		}
		
		static void AssignPatchTo(UDMXEntityFixturePatch* Patch, int32 ToAddress, int32 UniverseToAssignTo)
		{
			Patch->SetAutoStartingAddress(ToAddress);
			Patch->SetUniverseID(UniverseToAssignTo);
		}

		static bool FillIntoFirstGap(
			UDMXEntityFixturePatch* ToAssign,
			int32 UniverseToAssignTo,
			int32& MinimumAddress,
			int32& IndexOfFirstPatchWithMinAddress, 
			TArray<UDMXEntityFixturePatch*>& SortedPatchesWithSetAddress)
		{
			const int32 NeededSpan = ToAssign->GetChannelSpan();
			if (NeededSpan < 1)
			{
				return false;
			}

			UDMXEntityFixturePatch* FirstPatchWithMinAddress = SortedPatchesWithSetAddress[IndexOfFirstPatchWithMinAddress];
			const bool bDoesPatchFitBeforeFirstPatchWithMinAddress = FirstPatchWithMinAddress->GetStartingChannel() >= MinimumAddress
				&& FirstPatchWithMinAddress->GetStartingChannel() - MinimumAddress >= NeededSpan;
			if(bDoesPatchFitBeforeFirstPatchWithMinAddress)
			{
				AssignPatchTo(ToAssign, MinimumAddress, UniverseToAssignTo);
				SortedPatchesWithSetAddress.Insert(ToAssign, 0);

				MinimumAddress = ToAssign->GetEndingChannel() + 1;
				IndexOfFirstPatchWithMinAddress = 0;

				return true;
			}
			
			const bool bNoGapsToLookAt = IndexOfFirstPatchWithMinAddress == SortedPatchesWithSetAddress.Num() - 1;
			if(bNoGapsToLookAt)
			{
				return false;
			}
			
			int32 PrevAvailableAddress = GetEndAddressOf(SortedPatchesWithSetAddress[IndexOfFirstPatchWithMinAddress]) + 1;
			for (int32 iPatchInUniverse = IndexOfFirstPatchWithMinAddress + 1; iPatchInUniverse < SortedPatchesWithSetAddress.Num(); ++iPatchInUniverse)
			{
				const int32 NextStart = SortedPatchesWithSetAddress[iPatchInUniverse]->GetStartingChannel();
				const int32 UnoccupiedSpan = NextStart - PrevAvailableAddress;

				if (UnoccupiedSpan >= NeededSpan)
				{
					AssignPatchTo(ToAssign, PrevAvailableAddress, UniverseToAssignTo);
					SortedPatchesWithSetAddress.Insert(ToAssign, iPatchInUniverse);
					return true;
				}

				PrevAvailableAddress = GetEndAddressOf(SortedPatchesWithSetAddress[iPatchInUniverse]) + 1;
			}
			return false;
		}
		static int32 GetEndAddressOf(UDMXEntityFixturePatch* Patch)
		{
			return Patch->GetEndingChannel();
		}
	};

	if (ChangedFixturePatches.Num() == 0)
	{
		return {};
	}

	// Auto assign Patches from multiple Libraries is not supported
	UDMXLibrary* Library = ChangedFixturePatches[0]->GetParentLibrary();
	check(Library);
	for (UDMXEntityFixturePatch* Patch : ChangedFixturePatches)
	{
		check(Patch->GetParentLibrary() == Library);
	}

	// Only care about those that have auto assign addresses set
	TArray<UDMXEntityFixturePatch*> PatchesToAutoAssign = ChangedFixturePatches;
	PatchesToAutoAssign.RemoveAll([](UDMXEntityFixturePatch* Patch) {
		return !Patch->IsAutoAssignAddress();
		});
	
	TArray<UDMXEntityFixturePatch*> AllFixturePatches = Library->GetEntitiesTypeCast<UDMXEntityFixturePatch>();
	TMap<int32, TArray<UDMXEntityFixturePatch*>> UniverseToAllPatches = FDMXRuntimeUtils::MapToUniverses(AllFixturePatches);
	TArray<UDMXEntityFixturePatch*> PatchesToAssignInNextUniverse;
	for(auto UniverseIterator = UniverseToAllPatches.CreateIterator(); UniverseIterator; ++UniverseIterator)
	{
		const int32 CurrentUniverse = UniverseIterator->Key;
		TArray<UDMXEntityFixturePatch*>& SortedPatchesWithSetAddress = [&UniverseIterator, &PatchesToAutoAssign, CurrentUniverse]() -> TArray<UDMXEntityFixturePatch*>&
		{
			TArray<UDMXEntityFixturePatch*>& Result = UniverseIterator->Value;
			Result.RemoveAll([&, PatchesToAutoAssign](UDMXEntityFixturePatch* Patch) {
				return PatchesToAutoAssign.Contains(Patch);
				});
			Result.RemoveAll([&, PatchesToAutoAssign, CurrentUniverse](UDMXEntityFixturePatch* Patch) {
				return Patch->GetUniverseID() != CurrentUniverse;
				});
			Result.Sort([](const UDMXEntityFixturePatch& Patch, const UDMXEntityFixturePatch& Other) {
				return
					Patch.GetUniverseID() < Other.GetUniverseID() ||
					(Patch.GetUniverseID() == Other.GetUniverseID() && Patch.GetStartingChannel() <= Other.GetStartingChannel());
				}
			);
			return Result;
		}();
		TArray<UDMXEntityFixturePatch*> PatchesToAssignInThisUniverse = [&UniverseIterator, &PatchesToAutoAssign, CurrentUniverse]()
		{
			TArray<UDMXEntityFixturePatch*> Result = PatchesToAutoAssign;
			Result.RemoveAll([&, UniverseIterator](UDMXEntityFixturePatch* Patch)
				{
					return Patch->GetUniverseID() != CurrentUniverse;
				});
			return Result;
		}();

		if(bCanChangePatchUniverses)
		{
			// The patches in PatchesToAssignInNextUniverse are left over from last universe: hence they should ignore the MinimumAddress. 
			TArray<UDMXEntityFixturePatch*> ToAssignNextUniverse;
			Local::AssignAddressesInUniverse(SortedPatchesWithSetAddress, PatchesToAssignInNextUniverse, 1, CurrentUniverse, ToAssignNextUniverse);

			PatchesToAssignInNextUniverse = ToAssignNextUniverse;
		}
		Local::AssignAddressesInUniverse(SortedPatchesWithSetAddress, PatchesToAssignInThisUniverse, MinimumAddress, CurrentUniverse, PatchesToAssignInNextUniverse);
	}

	const bool bNeedNewUniverse = PatchesToAssignInNextUniverse.Num() > 0;
	if(bNeedNewUniverse)
	{
		if(bCanChangePatchUniverses)
		{
			const int32 HighestUniverse = [&UniverseToAllPatches]()
			{
				int32 HighestSoFar = -1;
				for (auto UniverseIterator = UniverseToAllPatches.CreateIterator(); UniverseIterator; ++UniverseIterator)
				{
					HighestSoFar = FMath::Max(HighestSoFar, UniverseIterator->Key);
				}
				return HighestSoFar;
			}();
			for(UDMXEntityFixturePatch* UnassignedPatch : PatchesToAssignInNextUniverse)
			{
				UnassignedPatch->Modify();
				UnassignedPatch->SetUniverseID(HighestUniverse + 1);
			}
			return AutoAssignedAddresses(PatchesToAssignInNextUniverse, 1, bCanChangePatchUniverses);
		}
		
		return PatchesToAssignInNextUniverse;
	}
	
	return {};
}

void FDMXEditorUtils::UpdatePatchColors(UDMXLibrary* Library)
{
	check(Library);

	TArray<UDMXEntityFixturePatch*> Patches = Library->GetEntitiesTypeCast<UDMXEntityFixturePatch>();
	for (UDMXEntityFixturePatch* Patch : Patches)
	{
		if (Patch->EditorColor == FLinearColor(1.0f, 0.0f, 1.0f))
		{
			FLinearColor NewColor;

			UDMXEntityFixturePatch** ColoredPatchOfSameType = Patches.FindByPredicate([&](const UDMXEntityFixturePatch* Other) {
				return Other != Patch &&
					Other->GetFixtureType() == Patch->GetFixtureType() &&
					Other->EditorColor != FLinearColor::White;
				});

			if (ColoredPatchOfSameType)
			{
				NewColor = (*ColoredPatchOfSameType)->EditorColor;
			}
			else
			{
				NewColor = FLinearColor::MakeRandomColor();

				// Avoid dominant red values for a bit more of a professional feel
				if (NewColor.R > 0.6f)
				{
					NewColor.R = FMath::Abs(NewColor.R - 1.0f);
				}
			}

			FProperty* ColorProperty = FindFProperty<FProperty>(UDMXEntityFixturePatch::StaticClass(), GET_MEMBER_NAME_CHECKED(UDMXEntityFixturePatch, EditorColor));

			Patch->Modify();
			Patch->PreEditChange(ColorProperty);
			Patch->EditorColor = NewColor;
			Patch->PostEditChange();
		}
	}
}

void FDMXEditorUtils::GetAllAssetsOfClass(UClass* Class, TArray<UObject*>& OutObjects)
{
	check(Class);

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(FName("AssetRegistry")).Get();
	TArray<FAssetData> OutAssets;
	AssetRegistry.GetAssetsByClass(Class->GetFName(), OutAssets, true);

	for (const FAssetData& Asset : OutAssets)
	{
		OutObjects.Add(Asset.GetAsset());
	}
}

bool FDMXEditorUtils::DoesLibraryHaveUniverseConflicts(UDMXLibrary* Library, FText& OutInputPortConflictMessage, FText& OutOutputPortConflictMessage)
{
	check(Library);

	OutInputPortConflictMessage = FText::GetEmpty();
	OutOutputPortConflictMessage = FText::GetEmpty();

	TArray<UObject*> LoadedLibraries;
	GetAllAssetsOfClass(UDMXLibrary::StaticClass(), LoadedLibraries);

	for (UObject* OtherLibrary : LoadedLibraries)
	{
		if (OtherLibrary == Library)
		{
			continue;
		}

		UDMXLibrary* OtherDMXLibrary = CastChecked<UDMXLibrary>(OtherLibrary);
		
		// Find conflicting input ports
		for (const FDMXInputPortSharedRef& InputPort : Library->GetInputPorts())
		{
			for (const FDMXInputPortSharedRef& OtherInputPort : OtherDMXLibrary->GetInputPorts())
			{
				if (InputPort->GetProtocol() == OtherInputPort->GetProtocol())
				{
					if (InputPort->GetLocalUniverseStart() <= OtherInputPort->GetLocalUniverseEnd() &&
						OtherInputPort->GetLocalUniverseStart() <= InputPort->GetLocalUniverseEnd())
					{
						continue;
					}

					if (OutInputPortConflictMessage.IsEmpty())
					{
						OutInputPortConflictMessage = LOCTEXT("LibraryInputPortUniverseConflictMessageStart", "Libraries use the same Input Port: ");
					}
					
					FText::Format(LOCTEXT("LibraryInputPortUniverseConflictMessage", "{0} {1}"), OutInputPortConflictMessage, FText::FromString(OtherDMXLibrary->GetName()));
				}
			}
		}

		// Find conflicting output ports
		for (const FDMXOutputPortSharedRef& OutputPort : Library->GetOutputPorts())
		{
			for (const FDMXOutputPortSharedRef& OtherOutputPort : OtherDMXLibrary->GetOutputPorts())
			{
				if (OutputPort->GetProtocol() == OtherOutputPort->GetProtocol())
				{
					if (OutputPort->GetLocalUniverseStart() <= OtherOutputPort->GetLocalUniverseEnd() &&
						OtherOutputPort->GetLocalUniverseStart() <= OutputPort->GetLocalUniverseEnd())
					{
						continue;
					}

					if (OutOutputPortConflictMessage.IsEmpty())
					{
						OutOutputPortConflictMessage = LOCTEXT("LibraryOutputPortUniverseConflictMessageStart", "Libraries that use the same Output Port: ");
					}

					FText::Format(LOCTEXT("LibraryOutputPortUniverseConflictMessage", "{0} {1}"), OutOutputPortConflictMessage, FText::FromString(OtherDMXLibrary->GetName()));
				}
			}
		}
	}

	bool bNoConflictsFound = OutOutputPortConflictMessage.IsEmpty() && OutInputPortConflictMessage.IsEmpty();

	return bNoConflictsFound;
}

void FDMXEditorUtils::ClearAllDMXPortBuffers()
{
	for (const FDMXInputPortSharedRef& InputPort : FDMXPortManager::Get().GetInputPorts())
	{
		InputPort->ClearBuffers();
	}

	for (const FDMXOutputPortSharedRef& OutputPort : FDMXPortManager::Get().GetOutputPorts())
	{
		OutputPort->ClearBuffers();
	}
}

void FDMXEditorUtils::ClearFixturePatchCachedData()
{
	// Clear patch buffers
	UDMXSubsystem* Subsystem = UDMXSubsystem::GetDMXSubsystem_Callable();
	if (Subsystem && Subsystem->IsValidLowLevel())
	{
		TArray<UDMXLibrary*> DMXLibraries = Subsystem->GetAllDMXLibraries();
		for (UDMXLibrary* Library : DMXLibraries)
		{
			if (Library != nullptr && Library->IsValidLowLevel())
			{
				Library->ForEachEntityOfType<UDMXEntityFixturePatch>([](UDMXEntityFixturePatch* Patch) {
					Patch->RebuildCache();
				});
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
