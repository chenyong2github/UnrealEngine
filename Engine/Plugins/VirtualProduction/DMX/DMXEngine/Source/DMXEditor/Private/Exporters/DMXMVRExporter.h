// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FDMXZipper;
class UDMXLibrary;
class UDMXMVRAssetImportData;
class UDMXMVRGeneralSceneDescription;

class FXmlFile;


/** Helper class to export a DMX Library as MVR file */
class FDMXMVRExporter
{
public:
	/** Exports the DMX Library as MVR File */
	static bool Export(UDMXLibrary* DMXLibrary, const FString& FilePathAndName);

private:
	/** Exports the DMX Library as MVR File */
	UE_NODISCARD bool ExportInternal(UDMXLibrary* DMXLibrary, const FString& FilePathAndName);

	/** Zips the GeneralSceneDescription.xml */
	UE_NODISCARD bool ZipGeneralSceneDescription(const TSharedRef<FDMXZipper>& Zip, const UDMXMVRGeneralSceneDescription* GeneralSceneDescription);

	/** Zips the GDTFs from the Library */
	void ZipGDTFs(const TSharedRef<FDMXZipper>& Zip, UDMXLibrary* DMXLibrary);

	/** Zips 3rd Party Data from the MVR Asset Import Data */
	void ZipThirdPartyData(const TSharedRef<FDMXZipper>& Zip, const UDMXMVRGeneralSceneDescription* GeneralSceneDescription);

	/** Creates an General Scene Description Xml File from the MVR Source, as it was imported */
	const TSharedPtr<FXmlFile> CreateSourceGeneralSceneDescriptionXmlFile(const UDMXMVRGeneralSceneDescription* GeneralSceneDescription) const;
};
