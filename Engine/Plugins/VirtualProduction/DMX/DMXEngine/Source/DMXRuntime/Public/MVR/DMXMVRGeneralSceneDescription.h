// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "MVR/DMXMVRFixture.h"

#include "Misc/Optional.h"
#include "Serialization/Archive.h"

#include "DMXMVRGeneralSceneDescription.generated.h"

class UDMXEntityFixturePatch;
class UDMXLibrary;
class UDMXMVRAssetImportData;

class FXmlFile;


/** MVR General Scene Description Object */
UCLASS()
class DMXRUNTIME_API UDMXMVRGeneralSceneDescription
	: public UObject
{
	GENERATED_BODY()

public:
	/** Constructor */
	UDMXMVRGeneralSceneDescription();

#if WITH_EDITOR
	/** Creates an MVR General Scene Description from an Xml File */
	static UDMXMVRGeneralSceneDescription* CreateFromXmlFile(TSharedRef<FXmlFile> GeneralSceneDescriptionXml, UObject* Outer, FName Name, EObjectFlags Flags = RF_NoFlags);

	/** Creates an MVR General Scene Description from a DMX Library */
	static UDMXMVRGeneralSceneDescription* CreateFromDMXLibrary(const UDMXLibrary& DMXLibrary, UObject* Outer, FName Name, EObjectFlags Flags = RF_NoFlags);
#endif 

	/** Returns a pointer to the MVR Fixture with corresponding UUID or nullptr if not found */
	FDMXMVRFixture* FindMVRFixture(const FGuid& MVRFixtureUUID);

	/** Adds an MVR Fixture to the General Scene Description */
	void AddMVRFixture(FDMXMVRFixture& MVRFixture);

#if WITH_EDITOR
	/**
	 * Writes the Library to the General Scene Description, effectively removing inexisting and adding
	 * new MVR Fixtures, according to what MVR Fixture UUIDs the Fixture Patches of the Library contain.
	 */
	void WriteDMXLibraryToGeneralSceneDescription(const UDMXLibrary& DMXLibrary);

	/** Returns the MVR Fixtures of the General Scene Description */
	FORCEINLINE const TArray<FDMXMVRFixture>& GetMVRFixtures() const { return MVRFixtures; }

	/** Returns MVR Asset Import Data for this asset */
	FORCEINLINE UDMXMVRAssetImportData* GetMVRAssetImportData() const { return MVRAssetImportData; }
#endif

protected:
	//~ Begin UObject interface
	virtual void Serialize(FArchive& Ar) override;
	//~ End UObject interface

private:
#if WITH_EDITOR
	/** Writes the Fixture Patch to the General Scene Description, adding a new MVR Fixture if required */
	void WriteFixturePatchToGeneralSceneDescription(const UDMXEntityFixturePatch& FixturePatch);

	/** Parses a General Scene Description Xml File. Only ment to be used for initialization (ensured) */
	void ParseGeneralSceneDescriptionXml(const TSharedRef<FXmlFile>& GeneralSceneDescriptionXml);
#endif

	/** Returns the Unit Numbers currently in use, sorted from lowest to highest */
	TArray<int32> GetUnitNumbersInUse(const UDMXLibrary& DMXLibrary);

	/** The MVR Fixtures of the General Scene Description */
	TArray<FDMXMVRFixture> MVRFixtures;

#if WITH_EDITORONLY_DATA
	/** Import Data for this asset */
	UPROPERTY()
	UDMXMVRAssetImportData* MVRAssetImportData;
#endif
};
