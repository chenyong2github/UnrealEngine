// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXTypes.h"

#include "CoreMinimal.h"
#include "EditorFramework/AssetImportData.h"

#include "DMXMVRAssetImportData.generated.h"

class FDMXXmlFile;
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

	/** Returns the raw source file as byte array, as it was imported */
	FORCEINLINE const TArray64<uint8>& GetRawSourceData() const { return RawSourceData.ByteArray; }
#endif // WITH_EDITOR 

private:
#if WITH_EDITORONLY_DATA
	/** The Asset Import Data used to generate the GDTF asset or nullptr, if not generated from a GDTF file */
	UPROPERTY()
	TObjectPtr<UAssetImportData> AssetImportData;

	/** The raw source file as byte array, as it was imported */
	UPROPERTY()
	FDMXByteArray64 RawSourceData;
#endif // WITH_EDITORONLY_DATA
};
