// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DatasmithAdditionalData.h"
#include "DatasmithCustomAction.h"
#include "DatasmithImportOptions.h"
#include "DatasmithUtils.h"

#include "CoreTechParametricSurfaceExtension.generated.h"


USTRUCT(BlueprintType)
struct DATASMITHCORETECHPARAMETRICSURFACEDATA_API FCoreTechSceneParameters
{
	GENERATED_BODY()

	// value from FDatasmithUtils::EModelCoordSystem
	UPROPERTY()
	uint8 ModelCoordSys = (uint8)FDatasmithUtils::EModelCoordSystem::ZUp_LeftHanded;

	UPROPERTY()
	float MetricUnit = 0.01f;

	UPROPERTY()
	float ScaleFactor = 1.0f;
};

USTRUCT()
struct DATASMITHCORETECHPARAMETRICSURFACEDATA_API FCoreTechMeshParameters
{
	GENERATED_BODY()

	UPROPERTY()
	bool bNeedSwapOrientation = false;

	UPROPERTY()
	bool bIsSymmetric = false;

	UPROPERTY()
	FVector SymmetricOrigin = FVector::ZeroVector;

	UPROPERTY()
	FVector SymmetricNormal = FVector::ZeroVector;
};


UCLASS(meta = (DisplayName = "Parametric Surface Data"))
class DATASMITHCORETECHPARAMETRICSURFACEDATA_API UCoreTechParametricSurfaceData : public UDatasmithAdditionalData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FString SourceFile;

	// Too costly to serialize as a UPROPERTY, will use custom serialization.
	TArray<uint8> RawData;

	UPROPERTY()
	FCoreTechSceneParameters SceneParameters;

	UPROPERTY()
	FCoreTechMeshParameters MeshParameters;

	UPROPERTY(EditAnywhere, Category=NURBS)
	FDatasmithTessellationOptions LastTessellationOptions;

private:
	UPROPERTY()
	TArray<uint8> RawData_DEPRECATED;

	virtual void Serialize(FArchive& Ar) override;
};

class IDatasmithMeshElement;
struct FDatasmithMeshElementPayload;

namespace CADLibrary
{
	struct FImportParameters;
	struct FMeshParameters;
}

namespace DatasmithCoreTechParametricSurfaceData
{
	void DATASMITHCORETECHPARAMETRICSURFACEDATA_API AddCoreTechSurfaceDataForMesh(const TSharedRef<IDatasmithMeshElement>& InMeshElement, const CADLibrary::FImportParameters& InSceneParameters, const CADLibrary::FMeshParameters&, const FDatasmithTessellationOptions& InTessellationOptions, FDatasmithMeshElementPayload& OutMeshPayload);
}
