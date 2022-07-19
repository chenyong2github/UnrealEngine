// Copyright Epic Games, Inc. All Rights Reserved.

#include "Exporters/DMXMVRExporter.h"

#include "DMXEditorLog.h"
#include "DMXMVRXmlMergeUtility.h"
#include "DMXZipper.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXGDTFAssetImportData.h"
#include "Library/DMXImportGDTF.h"
#include "Library/DMXLibrary.h"
#include "MVR/DMXMVRAssetImportData.h"
#include "MVR/DMXMVRGeneralSceneDescription.h"

#include "XmlFile.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"


bool FDMXMVRExporter::Export(UDMXLibrary* DMXLibrary, const FString& FilePathAndName)
{
	FDMXMVRExporter Instance;
	return Instance.ExportInternal(DMXLibrary, FilePathAndName);
}

bool FDMXMVRExporter::ExportInternal(UDMXLibrary* DMXLibrary, const FString& FilePathAndName)
{
	if (!ensureAlwaysMsgf(DMXLibrary, TEXT("Trying to export DMX Library '%s' as MVR file, but the DMX Library is invalid."), *DMXLibrary->GetName()))
	{
		return false;
	}

	DMXLibrary->UpdateGeneralSceneDescription();
	const UDMXMVRGeneralSceneDescription* GeneralSceneDescription = DMXLibrary->GetLazyGeneralSceneDescription();
	if (!ensureAlwaysMsgf(GeneralSceneDescription, TEXT("Trying to export DMX Library '%s' as MVR file, but its General Scene Description is invalid."), *DMXLibrary->GetName()))
	{
		return false;
	}

	const TSharedRef<FDMXZipper> Zip = MakeShared<FDMXZipper>();
	if (!ZipGeneralSceneDescription(Zip, GeneralSceneDescription))
	{
		return false;
	}

	ZipGDTFs(Zip, DMXLibrary);
	ZipThirdPartyData(Zip, GeneralSceneDescription);

	return Zip->SaveToFile(FilePathAndName);
}

bool FDMXMVRExporter::ZipGeneralSceneDescription(const TSharedRef<FDMXZipper>& Zip, const UDMXMVRGeneralSceneDescription* GeneralSceneDescription)
{
	TSharedPtr<FXmlFile> XmlFile = GeneralSceneDescription->CreateXmlFile();
	
	// Try to merge the source General Scene Description Xml
	const TSharedPtr<FXmlFile> SourceXmlFile = CreateSourceGeneralSceneDescriptionXmlFile(GeneralSceneDescription);
	if (SourceXmlFile.IsValid())
	{
		// Merge the General Scene Description with source data's xml (source) to retain 3rd party data
		XmlFile = FDMXXmlMergeUtility::Merge(GeneralSceneDescription, SourceXmlFile.ToSharedRef());
	}
	
	// Create a temp GeneralSceneDescription.xml file
	const FString TempPath = FPaths::ConvertRelativePathToFull(FPaths::EngineSavedDir() / TEXT("DMX_Temp"));
	constexpr TCHAR GeneralSceneDescriptionFileName[] = TEXT("GeneralSceneDescription.xml");
	const FString TempGeneralSceneDescriptionFilePathAndName = TempPath / GeneralSceneDescriptionFileName;
	if (!XmlFile->Save(TempGeneralSceneDescriptionFilePathAndName))
	{
		UE_LOG(LogDMXEditor, Error, TEXT("Failed to save General Scene Description. See previous errors for details."));
		return false;
	}

	TArray64<uint8> GeneralSceneDescriptionData;
	FFileHelper::LoadFileToArray(GeneralSceneDescriptionData, *TempGeneralSceneDescriptionFilePathAndName);
	Zip->AddFile(GeneralSceneDescriptionFileName, GeneralSceneDescriptionData);

	// Delete temp GeneralSceneDescription.xml file
	IFileManager& FileManager = IFileManager::Get();
	constexpr bool bRequireExists = true;
	constexpr bool bEvenIfReadOnly = false;
	constexpr bool bQuiet = true;
	FileManager.Delete(*TempGeneralSceneDescriptionFilePathAndName, bRequireExists, bEvenIfReadOnly, bQuiet);

	return true;
}

void FDMXMVRExporter::ZipGDTFs(const TSharedRef<FDMXZipper>& Zip, UDMXLibrary* DMXLibrary)
{
	check(DMXLibrary);

	// Gather GDTFs
	TArray<const UDMXGDTFAssetImportData*> GDTFAssetImportDataToExport;
	const TArray<const UDMXEntityFixturePatch*> FixturePatches = DMXLibrary->GetEntitiesTypeCast<const UDMXEntityFixturePatch>();
	for (const UDMXEntityFixturePatch* FixturePatch : FixturePatches)
	{
		const UDMXEntityFixtureType* FixtureType = FixturePatch->GetFixtureType();
		if (!FixtureType)
		{
			continue;
		}
		else if (!FixtureType->GDTF)
		{
			UE_LOG(LogDMXEditor, Warning, TEXT("Cannot export Fixture Patch '%s' to MVR. Its Fixture Type '%s' has no GDTF set."), *FixturePatch->Name, *FixtureType->Name);
			continue;
		}

		const UDMXGDTFAssetImportData* GDTFAssetImportData = FixtureType->GDTF->GetGDTFAssetImportData();
		if (!GDTFAssetImportData || GDTFAssetImportData->GetRawSourceData().IsEmpty())
		{
			UE_LOG(LogDMXEditor, Warning, TEXT("Cannot export Fixture Patch '%s' to MVR. Its Fixture Type '%' is from a version prior to 5.1, and does not hold required GDTF data."), *FixturePatch->Name, *FixtureType->Name);
			continue;
		}

		GDTFAssetImportDataToExport.AddUnique(GDTFAssetImportData);
	}

	// Zip GDTFs
	TArray<FString> TempGDTFFilePathAndNames;
	for (const UDMXGDTFAssetImportData* GDTFAssetImportData : GDTFAssetImportDataToExport)
	{
		if (GDTFAssetImportData->GetRawSourceData().Num() > 0)
		{
			const FString GDTFFilename = FPaths::GetCleanFilename(GDTFAssetImportData->GetSourceFilePathAndName());
			Zip->AddFile(GDTFFilename, GDTFAssetImportData->GetRawSourceData());
		}
	}
}

void FDMXMVRExporter::ZipThirdPartyData(const TSharedRef<FDMXZipper>& Zip, const UDMXMVRGeneralSceneDescription* GeneralSceneDescription)
{
	const UDMXMVRAssetImportData* AssetImportData = GeneralSceneDescription->GetMVRAssetImportData();
	if (!AssetImportData)
	{
		return;
	}

	if (AssetImportData && AssetImportData->GetRawSourceData().Num() > 0)
	{
		const TSharedRef<FDMXZipper> SourceZip = MakeShared<FDMXZipper>();
		if (SourceZip->LoadFromData(AssetImportData->GetRawSourceData()))
		{
			for (const FString& SourceFileName : SourceZip->GetFiles())
			{
				// Don't add GeneralSceneDescription.xml and GDTFs
				constexpr TCHAR GeneralSceneDescriptionFileName[] = TEXT("GeneralSceneDescription.xml");
				constexpr TCHAR GDTFExtension[] = TEXT("gdtf");
				if (SourceFileName.EndsWith(GeneralSceneDescriptionFileName) || FPaths::GetExtension(SourceFileName) == GDTFExtension)
				{
					continue;
				}

				TArray64<uint8> SourceFileData;
				if (SourceZip->GetFileContent(SourceFileName, SourceFileData))
				{
					Zip->AddFile(SourceFileName, SourceFileData);
				}
			}
		}
	}
}

const TSharedPtr<FXmlFile> FDMXMVRExporter::CreateSourceGeneralSceneDescriptionXmlFile(const UDMXMVRGeneralSceneDescription* GeneralSceneDescription) const
{
	const UDMXMVRAssetImportData* AssetImportData = GeneralSceneDescription->GetMVRAssetImportData();
	if (!AssetImportData)
	{
		return nullptr;
	}

	if (AssetImportData->GetRawSourceData().IsEmpty())
	{
		return nullptr;
	}

	const TSharedRef<FDMXZipper> DMXZipper = MakeShared<FDMXZipper>();
	if (!DMXZipper->LoadFromData(AssetImportData->GetRawSourceData()))
	{
		return nullptr;
	}

	constexpr TCHAR GeneralSceneDescriptionFilename[] = TEXT("GeneralSceneDescription.xml");
	FDMXZipper::FDMXScopedUnzipToTempFile UnzipTempFileScope(DMXZipper, GeneralSceneDescriptionFilename);

	if (UnzipTempFileScope.TempFilePathAndName.IsEmpty())
	{
		return nullptr;
	}

	const TSharedRef<FXmlFile> XmlFile = MakeShared<FXmlFile>();
	if (!XmlFile->LoadFile(UnzipTempFileScope.TempFilePathAndName))
	{
		return nullptr;
	}

	return XmlFile;
}
