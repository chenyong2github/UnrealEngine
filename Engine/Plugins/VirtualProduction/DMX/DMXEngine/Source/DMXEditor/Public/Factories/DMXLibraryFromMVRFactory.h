// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"

#include "DMXLibraryFromMVRFactory.generated.h"

class FDMXZipper;
class UDMXImportGDTF;
class UDMXLibrary;
class UDMXMVRGeneralSceneDescription;


UCLASS()
class DMXEDITOR_API UDMXLibraryFromMVRFactory 
	: public UFactory
{
	GENERATED_BODY()

public:
	UDMXLibraryFromMVRFactory();

	//~ Begin UFactory Interface
	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* Parent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled);
	virtual bool FactoryCanImport(const FString& Filename) override;
	//~ End UFactory Interface	

	/** File extention for MVR files */
	static const FName MVRFileExtension;

	/** File extention for GDTF files */
	static const FName GDTFFileExtension;

private:
	/** Creates a DMX Library asset. Returns nullptr if the library could not be created */
	UDMXLibrary* CreateDMXLibraryAsset(UObject* Parent, EObjectFlags Flags, const FString& InFilename);

	/** Creates GDTF assets from the MVR */
	TArray<UDMXImportGDTF*> CreateGDTFAssets(UObject* Parent, EObjectFlags Flags, const TSharedRef<FDMXZipper>& Zip, const UDMXMVRGeneralSceneDescription& GeneralSceneDescription);

	/** Initializes the DMX Library from the General Scene Description and GDTF assets */
	void InitDMXLibrary(UDMXLibrary* DMXLibrary, const TArray<UDMXImportGDTF*>& GDTFAssets, UDMXMVRGeneralSceneDescription* GeneralSceneDescription) const;

	/** The DMX Library Package Name, initialized when the DMX Library Asset is created */
	FString DMXLibraryPackageName;
};
