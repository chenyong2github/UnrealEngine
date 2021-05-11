// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ParametricSurfaceExtension.h"
#include "DatasmithAdditionalData.h"
#include "DatasmithCustomAction.h"
#include "DatasmithImportOptions.h"
#include "DatasmithUtils.h"

#include "CADKernelSurfaceExtension.generated.h"

UCLASS(meta = (DisplayName = "CADKernel Parametric Surface Data"))
class CADKERNELSURFACE_API UCADKernelParametricSurfaceData : public UParametricSurfaceData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FString SourceFile;

	// Too costly to serialize as a UPROPERTY, will use custom serialization.
	TArray<uint8> RawData;

	virtual bool IsValid() override
	{
		return RawData.Num() > 0;
	}
	
	virtual bool Tessellate(UStaticMesh& StaticMesh, const FDatasmithRetessellationOptions& RetessellateOptions) override;

private:

	virtual void Serialize(FArchive& Ar) override;
};

class IDatasmithMeshElement;
struct FDatasmithMeshElementPayload;

namespace CADLibrary
{
	struct FImportParameters;
	struct FMeshParameters;
}

namespace CADKernelSurface
{
	void CADKERNELSURFACE_API AddSurfaceDataForMesh(const TSharedRef<IDatasmithMeshElement>& InMeshElement, const CADLibrary::FImportParameters& InSceneParameters, const CADLibrary::FMeshParameters&, const FDatasmithTessellationOptions& InTessellationOptions, FDatasmithMeshElementPayload& OutMeshPayload);
}
