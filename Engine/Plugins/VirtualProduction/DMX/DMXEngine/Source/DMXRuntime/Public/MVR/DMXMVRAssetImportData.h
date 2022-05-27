// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorFramework/AssetImportData.h"

#include "DMXMVRAssetImportData.generated.h"

class UDMXMVRGeneralSceneDescription;


UCLASS()
class DMXRUNTIME_API UDMXMVRAssetImportData 
	: public UAssetImportData
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	/** Sets the source file and initializes members from it */
	void SetSourceFile(const FString& FilePathAndName);

	/** Returns the Asset Import Data */
	FORCEINLINE UAssetImportData* GetAssetImportData() { return AssetImportData;};

	/** Returns the raw zip file the asset was generated from */
	FORCEINLINE const TArray<uint8>& GetRawZipFile() const { return RawZipFile; }
#endif // WITH_EDITOR 

private:
#if WITH_EDITORONLY_DATA
	/** The Asset Import Data used to generate the GDTF asset or nullptr, if not generated from a GDTF file */
	UPROPERTY()
	UAssetImportData* AssetImportData;

	/** The raw GDTF zip file as byte array */
	UPROPERTY()
	TArray<uint8> RawZipFile;
#endif // WITH_EDITORONLY_DATA
};
