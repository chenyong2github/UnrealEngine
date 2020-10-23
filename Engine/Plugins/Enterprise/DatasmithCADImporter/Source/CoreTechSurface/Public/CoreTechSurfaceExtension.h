// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DatasmithCustomAction.h"
#include "DatasmithAdditionalData.h"
#include "DatasmithImportOptions.h"

#include "CoreTechSurfaceExtension.generated.h"


USTRUCT(BlueprintType)
struct CORETECHSURFACE_API FCoreTechSceneParameters
{
	GENERATED_BODY()

	// value from FDatasmithUtils::EModelCoordSystem
	UPROPERTY()
	uint8 ModelCoordSys;

	UPROPERTY()
	float MetricUnit;

	UPROPERTY()
	float ScaleFactor;
};

USTRUCT()
struct CORETECHSURFACE_API FCoreTechMeshParameters
{
	GENERATED_BODY()

	UPROPERTY()
	bool bNeedSwapOrientation;

	UPROPERTY()
	bool bIsSymmetric;

	UPROPERTY()
	FVector SymmetricOrigin;

	UPROPERTY()
	FVector SymmetricNormal;
};


UCLASS(meta = (DisplayName = "Parametric Surface Data"))
class CORETECHSURFACE_API UCoreTechParametricSurfaceData : public UDatasmithAdditionalData
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

namespace CoreTechSurface
{
	void CORETECHSURFACE_API AddCoreTechSurfaceDataForMesh(const TSharedRef<IDatasmithMeshElement>& InMeshElement, const CADLibrary::FImportParameters& InSceneParameters, const CADLibrary::FMeshParameters&, const FDatasmithTessellationOptions& InTessellationOptions, FDatasmithMeshElementPayload& OutMeshPayload);
}
