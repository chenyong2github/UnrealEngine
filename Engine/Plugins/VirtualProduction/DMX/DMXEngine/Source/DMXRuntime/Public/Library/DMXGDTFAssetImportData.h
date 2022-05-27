// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorFramework/AssetImportData.h"

#include "DMXGDTFAssetImportData.generated.h"

class UDMXImportGDTF;


UCLASS()
class DMXRUNTIME_API UDMXGDTFAssetImportData 
	: public UAssetImportData
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	/** Returns path and name of the source file the GDTF asset originated from */
	FString GetSourceFilePathAndName() const;

	/** Creates new asset GDTF Asset Import Data from the File. Returns the GDTF Asset Import Data or nullptr if not a valid GDTF File */
	void SetSourceFile(const FString& FilePathAndName);

	/** Returns the raw zip file the asset was generated from */
	FORCEINLINE const TArray<uint8>& GetRawZipFile() const { return RawZipFile; }
#endif // WITH_EDITOR 

private:
	/** We need to remember the literal File Name to */


#if WITH_EDITORONLY_DATA
	/** The raw GDTF zip file as byte array */
	UPROPERTY()
	TArray<uint8> RawZipFile;
#endif // WITH_EDITORONLY_DATA
};
