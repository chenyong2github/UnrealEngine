// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factories/DMXLibraryFromMVRFactory.h"

#include "DMXEditorLog.h"
#include "DMXEditorModule.h"
#include "DMXEditorUtils.h"
#include "Factories/DMXGDTFFactory.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXImportGDTF.h"
#include "Library/DMXLibrary.h"
#include "MVR/DMXMVRGeneralSceneDescription.h"
#include "MVR/DMXMVRUnzip.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "ObjectTools.h"
#include "XmlFile.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"


#define LOCTEXT_NAMESPACE "DMXLibraryFromMVRFactory"

const FName UDMXLibraryFromMVRFactory::MVRFileExtension = "MVR";
const FName UDMXLibraryFromMVRFactory::GDTFFileExtension = "GDTF";

UDMXLibraryFromMVRFactory::UDMXLibraryFromMVRFactory()
{
	bEditorImport = true;
	bEditAfterNew = true;
	SupportedClass = UDMXLibrary::StaticClass();

	Formats.Add(TEXT("mvr;My Virtual Rig"));
}

UObject* UDMXLibraryFromMVRFactory::FactoryCreateFile(UClass* InClass, UObject* Parent, FName InName, EObjectFlags Flags, const FString& InFilename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	bOutOperationCanceled = false;
	CurrentFilename = InFilename;

	UDMXLibrary* NewDMXLibrary = CreateDMXLibraryAsset(Parent, Flags, InFilename);
	if (!NewDMXLibrary)
	{
		UE_LOG(LogDMXEditor, Error, TEXT("Failed to create DMX Library for MVR '%s'."), *InFilename);
		return nullptr;
	}

	const TSharedPtr<FDMXMVRUnzip> MVRUnzip = FDMXMVRUnzip::CreateFromFile(InFilename);
	if (!MVRUnzip.IsValid())
	{
		UE_LOG(LogDMXEditor, Error, TEXT("Cannot read MVR '%s'. File is not a valid MVR."), *InFilename);
		return nullptr;
	}

	TArray64<uint8> XMLData;
	MVRUnzip->GetFileContent(TEXT("GeneralSceneDescription.xml"), XMLData);

	// MVR implicitly adpots UTF-8 encoding of Xml Files by adopting the GDT standard (DIN-15800).
	// Content is NOT null-terminated; we need to specify lengths here.
	const FUTF8ToTCHAR TCHARData(reinterpret_cast<const ANSICHAR*>(XMLData.GetData()), XMLData.Num());

	const TSharedRef<FXmlFile> GeneralSceneDescriptionXml = MakeShared<FXmlFile>(FString(TCHARData.Length(), TCHARData.Get()), EConstructMethod::ConstructFromBuffer);
	FDMXMVRGeneralSceneDescription GeneralSceneDescription(GeneralSceneDescriptionXml);

	TArray<UDMXImportGDTF*> GDTFs = CreateGDTFAssets(Parent, Flags, MVRUnzip.ToSharedRef(), GeneralSceneDescription);

	InitDMXLibrary(NewDMXLibrary, GDTFs, GeneralSceneDescription);

	return NewDMXLibrary;
}

bool UDMXLibraryFromMVRFactory::FactoryCanImport(const FString& Filename)
{
	const FString TargetExtension = FPaths::GetExtension(Filename);

	if (TargetExtension.Equals(UDMXLibraryFromMVRFactory::MVRFileExtension.ToString(), ESearchCase::IgnoreCase))
	{
		return true;
	}

	return false;
}

UDMXLibrary* UDMXLibraryFromMVRFactory::CreateDMXLibraryAsset(UObject* Parent, EObjectFlags Flags, const FString& InFilename)
{
	constexpr bool bRemovePathFromDesiredName = true;
	const FString BaseFileName = FPaths::GetBaseFilename(InFilename, bRemovePathFromDesiredName);
	const FString PackageName = Parent->GetName() + TEXT("_MVR/");
	const FString AssetName = ObjectTools::SanitizeObjectName(BaseFileName);

	FString UniquePackageName;
	FString UniqueAssetName;
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(PackageName / AssetName, TEXT(""), UniquePackageName, UniqueAssetName);

	UPackage* Package = CreatePackage(*UniquePackageName);
	check(Package);
	Package->FullyLoad();

	UImportSubsystem* ImportSubsystem = GEditor->GetEditorSubsystem<UImportSubsystem>();
	ImportSubsystem->BroadcastAssetPreImport(this, UDMXLibrary::StaticClass(), Parent, *UniqueAssetName, *MVRFileExtension.ToString());

	UDMXLibrary* NewDMXLibrary = NewObject<UDMXLibrary>(Package, *UniqueAssetName, Flags | RF_Public);
	if (!NewDMXLibrary)
	{
		return nullptr;
	}

	ImportSubsystem->BroadcastAssetPostImport(this, NewDMXLibrary);
	
	return NewDMXLibrary;
}

TArray<UDMXImportGDTF*> UDMXLibraryFromMVRFactory::CreateGDTFAssets(UObject* Parent, EObjectFlags Flags, const TSharedRef<FDMXMVRUnzip>& MVRUnzip, const FDMXMVRGeneralSceneDescription& GeneralSceneDescription)
{
	TArray<UDMXImportGDTF*> ImportedGDTFs;
	TArray<FString> ImportedGDTFNames;

	constexpr bool bRemovePathFromDesiredName = true;
	const FString PackageName = Parent->GetName() + FString(TEXT("_MVR")) / FString(TEXT("GDTFs"));

	TArray<FAssetData> ExistingGDTFAssets;
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistryModule.Get().GetAssetsByPath(FName(PackageName), ExistingGDTFAssets);

	TArray<UDMXImportGDTF*> ExistingGDTFs;
	for (const FAssetData& AssetData : ExistingGDTFAssets)
	{
		if (UDMXImportGDTF* GDTFAsset = Cast<UDMXImportGDTF>(AssetData.GetAsset()))
		{
			GeneralSceneDescription.MVRFixtures.ContainsByPredicate([GDTFAsset](const FDMXMVRFixture& MVRFixture)
				{
					return GDTFAsset->SourceFilename.Equals(MVRFixture.GDTFSpec, ESearchCase::IgnoreCase);
				});

			ExistingGDTFs.AddUnique(GDTFAsset);
		}
	}

	UDMXGDTFFactory* GDTFFactory = NewObject<UDMXGDTFFactory>();
	if (ExistingGDTFs.Num() > 0)
	{
		const FText MessageText = LOCTEXT("MVRImportReimportsGDTFDialog", "MVR contains existing GDTFs. Do you want to reimport the existing GDTF assets?");
		if (FMessageDialog::Open(EAppMsgType::YesNo, MessageText) == EAppReturnType::Yes)
		{
			for (UDMXImportGDTF* GDTFAsset : ExistingGDTFs)
			{
				if (GDTFFactory->Reimport(GDTFAsset) == EReimportResult::Succeeded)
				{
					ImportedGDTFs.Add(GDTFAsset);
					ImportedGDTFNames.Add(GDTFAsset->SourceFilename);
				}
			}
		}
	}

	// Import GDTF Assets that aren't yet imported in the reimport procedure
	for (const FDMXMVRFixture& MVRFixture : GeneralSceneDescription.MVRFixtures)
	{
		// Don't import the same GDTF twice
		if (!ImportedGDTFNames.Contains(MVRFixture.GDTFSpec))
		{
			const FDMXMVRUnzip::FDMXScopedUnzipToTempFile ScopedUnzipGDTF(MVRUnzip, MVRFixture.GDTFSpec);
			if (!ScopedUnzipGDTF.TempFilePathAndName.IsEmpty())
			{
				UPackage* Package = FDMXEditorUtils::GetOrCreatePackage(Parent, MVRFixture.GDTFSpec);

				bool bCanceled;
				UObject* NewGDTFObject = GDTFFactory->FactoryCreateFile(UDMXImportGDTF::StaticClass(), Package, FName(*MVRFixture.GDTFSpec), Flags | RF_Public, ScopedUnzipGDTF.TempFilePathAndName, nullptr, GWarn, bCanceled);
				
				if (UDMXImportGDTF* NewGDTF = Cast<UDMXImportGDTF>(NewGDTFObject))
				{
					ImportedGDTFs.Add(NewGDTF);
					ImportedGDTFNames.Add(MVRFixture.GDTFSpec);

					FAssetRegistryModule::AssetCreated(NewGDTF);
					Package->MarkPackageDirty();
				}
			}
		}
	}

	return ImportedGDTFs;
}

void UDMXLibraryFromMVRFactory::InitDMXLibrary(UDMXLibrary* DMXLibrary, const TArray<UDMXImportGDTF*>& GDTFAssets, const FDMXMVRGeneralSceneDescription& GeneralSceneDescription) const
{
	if (DMXLibrary)
	{
		// Create a Fixture Type for each GDTF
		TMap<FString, UDMXEntityFixtureType*> GDTFSpecToFixtureTypeMap;
		for (UDMXImportGDTF* GDTF : GDTFAssets)
		{
			const FString GDTFFilename = FPaths::GetCleanFilename(GDTF->SourceFilename);
			FDMXEntityFixtureTypeConstructionParams FixtureTypeConstructionParams;
			FixtureTypeConstructionParams.DMXCategory = FDMXFixtureCategory(FDMXFixtureCategory::GetFirstValue());
			FixtureTypeConstructionParams.ParentDMXLibrary = DMXLibrary;

			UDMXEntityFixtureType* FixtureType = UDMXEntityFixtureType::CreateFixtureTypeInLibrary(FixtureTypeConstructionParams, FPaths::GetBaseFilename(GDTFFilename));
			FixtureType->SetGDTF(GDTF);

			GDTFSpecToFixtureTypeMap.Add(GDTFFilename, FixtureType);
		}

		// Create Fixture Patches for the MVR Fixtures
		for (const FDMXMVRFixture& MVRFixture : GeneralSceneDescription.MVRFixtures)
		{
			if (!GDTFSpecToFixtureTypeMap.Contains(MVRFixture.GDTFSpec))
			{
				continue;
			}

			UDMXEntityFixtureType* FixtureType = GDTFSpecToFixtureTypeMap[MVRFixture.GDTFSpec];
			UDMXEntityFixturePatch* const* FixturePatchPtr = DMXLibrary->GetEntitiesTypeCast<UDMXEntityFixturePatch>().FindByPredicate([MVRFixture, FixtureType](UDMXEntityFixturePatch* FixturePatch)
				{
					const FDMXFixtureMode* ActiveModePtr = FixturePatch->GetActiveMode();
					return
						FixturePatch->GetFixtureType() == FixtureType &&
						FixturePatch->GetUniverseID() == MVRFixture.Addresses.Universe &&
						FixturePatch->GetStartingChannel() == MVRFixture.Addresses.Address &&
						ActiveModePtr && ActiveModePtr->ModeName == MVRFixture.GDTFMode;
				});
			
			if (!FixturePatchPtr)
			{
				int32 ActiveModeIndex = FixtureType->Modes.IndexOfByPredicate([MVRFixture](const FDMXFixtureMode& Mode)
					{
						return Mode.ModeName == MVRFixture.GDTFMode;
					});

				if (ActiveModeIndex != INDEX_NONE)
				{
					FDMXEntityFixturePatchConstructionParams FixturePatchConstructionParams;
					FixturePatchConstructionParams.ActiveMode = ActiveModeIndex;
					FixturePatchConstructionParams.FixtureTypeRef = FDMXEntityFixtureTypeRef(FixtureType);
					FixturePatchConstructionParams.UniverseID = MVRFixture.Addresses.Universe;
					FixturePatchConstructionParams.StartingAddress = MVRFixture.Addresses.Address;

					UDMXEntityFixturePatch* FixturePatch = UDMXEntityFixturePatch::CreateFixturePatchInLibrary(FixturePatchConstructionParams, MVRFixture.Name);
					FixturePatchPtr = &FixturePatch;
				}
			}
			check(FixturePatchPtr);

			(*FixturePatchPtr)->AddMVRFixtureInstance(MVRFixture);
		}
	}
}

#undef LOCTEXT_NAMESPACE
