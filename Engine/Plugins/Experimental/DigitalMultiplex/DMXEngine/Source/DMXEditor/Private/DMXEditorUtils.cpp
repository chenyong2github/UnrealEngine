// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXEditorUtils.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityFader.h"
#include "Library/DMXEntityController.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXEntityFixturePatch.h"

#include "ScopedTransaction.h"
#include "UnrealExporter.h"
#include "Exporters/Exporter.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Factories.h"
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


bool FDMXEditorUtils::GetNameAndIndexFromString(const FString& InString, FString& OutName, int32& OutIndex)
{
	OutName = InString.TrimEnd();

	// If there's an index at the end of the name, erase it
	int32 DigitIndex = OutName.Len();
	while (DigitIndex > 0 && OutName[DigitIndex - 1] >= '0' && OutName[DigitIndex - 1] <= '9')
	{
		--DigitIndex;
	}

	bool bHadIndex = false;
	if (DigitIndex < OutName.Len() && DigitIndex > -1)
	{
		OutIndex = FCString::Atoi(*OutName.RightChop(DigitIndex));
		OutName = OutName.Left(DigitIndex);
		bHadIndex = true;
	}
	else
	{
		OutIndex = 0;
	}

	if (OutName.EndsWith(TEXT("_")))
	{
		OutName = OutName.LeftChop(1);
	}
	OutName.TrimEnd();

	return bHadIndex;
}

FString FDMXEditorUtils::GenerateUniqueNameFromExisting(const TSet<FString>& InExistingNames, const FString& InBaseName)
{
	if (!InBaseName.IsEmpty() && !InExistingNames.Contains(InBaseName))
	{
		return InBaseName;
	}

	FString FinalName;
	FString BaseName;

	if (InBaseName.IsEmpty())
	{
		BaseName = TEXT("Default name");
	}
	else
	{
		// If there's an index at the end of the name, erase it
		int32 Index = 0;
		GetNameAndIndexFromString(InBaseName, BaseName, Index);
	}

	int32 Count = 1;
	FinalName = BaseName;
	// Add Count to the BaseName, increasing Count, until it's a non-existent name
	do
	{
		// Calculate the number of digits in the number, adding 2 (1 extra to correctly count digits, another to account for the '_' that will be added to the name
		int32 CountLength = Count > 0 ? (int32)FGenericPlatformMath::LogX(10.0f, Count) + 2 : 2;

		// If the length of the final string will be too long, cut off the end so we can fit the number
		if (CountLength + BaseName.Len() > NAME_SIZE)
		{
			BaseName = BaseName.Left(NAME_SIZE - CountLength);
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

	// Iterate over all of the Fixture's Modes, Functions and Sub Functions, creating names for the
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

			// Name this function's Sub Functions
			TSet<FString> SubFunctionsNames;
			for (FDMXFixtureSubFunction& SubFunction : Function.SubFunctions)
			{
				if (SubFunction.FunctionName.IsEmpty())
				{
					// Cache existing names only once, when needed.
					if (SubFunctionsNames.Num() == 0)
					{
						for (FDMXFixtureSubFunction& NamedSubFunction : Function.SubFunctions)
						{
							if (!NamedSubFunction.FunctionName.IsEmpty())
							{
								SubFunctionsNames.Add(NamedSubFunction.FunctionName);
							}
						}
					}

					SubFunction.FunctionName = GenerateUniqueNameFromExisting(SubFunctionsNames, TEXT("SubFunction"));
					SubFunctionsNames.Add(SubFunction.FunctionName);
				}
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
	if (NewEntityName.Len() > NAME_SIZE)
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

UDMXEntityFader* FDMXEditorUtils::CreateFaderTemplate(const UDMXLibrary* InLibrary)
{
	FString BaseName(TEXT(""));
	FString EntityName = FindUniqueEntityName(InLibrary, UDMXEntityFader::StaticClass(), BaseName);

	UDMXEntityFader* OutputConsoleFaderTemplate = NewObject<UDMXEntityFader>(GetTransientPackage(), UDMXEntityFader::StaticClass(), NAME_None, RF_Transient);
	OutputConsoleFaderTemplate->SetName(EntityName);

	return OutputConsoleFaderTemplate;
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
					if (Patch->ParentFixtureTypeTemplate == InEntity)
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

void FDMXEditorUtils::RemoveEntities(UDMXLibrary* InLibrary, const TArray<UDMXEntity*>&& InEntities)
{
	if (InLibrary != nullptr)
	{
		for (UDMXEntity* EntityToDelete : InEntities)
		{
			// Fix references to this Entity
			if (UDMXEntityFixtureType* AsFixtureType = Cast<UDMXEntityFixtureType>(EntityToDelete))
			{
				// Find Fixture Patches using this Fixture Type and null their templates
				InLibrary->ForEachEntityOfType<UDMXEntityFixturePatch>([&AsFixtureType](UDMXEntityFixturePatch* Patch)
					{
						if (Patch->ParentFixtureTypeTemplate == AsFixtureType)
						{
							Patch->Modify();
							Patch->ParentFixtureTypeTemplate = nullptr;
						}
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
			if (AsPatch->ParentFixtureTypeTemplate != nullptr)
			{
				bool bExportedTemplate = false;

				// Try to get a cached duplicate of the template
				UDMXEntityFixtureType* DuplicateFixtureType;
				if (UDMXEntityFixtureType** CachedTemplate = CopiedPatchTemplates.Find(AsPatch->ParentFixtureTypeTemplate->GetFName()))
				{
					DuplicateFixtureType = *CachedTemplate;
					bExportedTemplate = true;
				}
				else
				{
					// Copy the template to the transient package to make the Patch reference the copy
					FObjectDuplicationParameters DuplicationParams(AsPatch->ParentFixtureTypeTemplate, GetTransientPackage());
					DuplicationParams.DestName = AsPatch->ParentFixtureTypeTemplate->GetFName();

					DuplicateFixtureType = CastChecked<UDMXEntityFixtureType>(StaticDuplicateObjectEx(DuplicationParams));
					// Keep same entity ID to find the original Template when pasting
					DuplicateFixtureType->ReplicateID(AsPatch->ParentFixtureTypeTemplate);

					// Cache this copy so we don't copy the same template over and over for several Patches
					CopiedPatchTemplates.Add(DuplicateFixtureType->GetFName(), DuplicateFixtureType);
				}

				// We'll temporarily change the ParentFixtureTypeTemplate of the Patch to copy it
				// with a reference to the duplicate Fixture Type
				UDMXEntityFixtureType* OriginalTemplate = AsPatch->ParentFixtureTypeTemplate;
				AsPatch->ParentFixtureTypeTemplate = DuplicateFixtureType;
				// Export the Patch referencing the duplicate template
				UExporter::ExportToOutputDevice(&Context, AsPatch, nullptr, Archive, TEXT("copy"), 0, PPF_ExportsNotFullyQualified | PPF_Copy | PPF_Delimited, false, GetTransientPackage());
				if (!bExportedTemplate)
				{
					// Export the template after the Patch, to make interpretation easier when pasting it back
					UExporter::ExportToOutputDevice(&Context, DuplicateFixtureType, nullptr, Archive, TEXT("copy"), 4, PPF_ExportsNotFullyQualified | PPF_Copy | PPF_Delimited, false, GetTransientPackage());
				}
				// Revert the patch to it's original, private template
				AsPatch->ParentFixtureTypeTemplate = OriginalTemplate;
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
	if (EntityClass->IsChildOf(UDMXEntityController::StaticClass()))
	{
		return FText::Format(
			LOCTEXT("EntityTypeName_Controller", "{0}|plural(one=Controller, other=Controllers)"),
			bPlural ? 2 : 1
		);
	}
	else if (EntityClass->IsChildOf(UDMXEntityFixtureType::StaticClass()))
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

#undef LOCTEXT_NAMESPACE
