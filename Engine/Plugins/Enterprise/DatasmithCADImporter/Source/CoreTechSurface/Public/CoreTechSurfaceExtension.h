// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ParametricSurfaceExtension.h"
#include "DatasmithAdditionalData.h"
#include "DatasmithCustomAction.h"
#include "DatasmithImportOptions.h"
#include "DatasmithUtils.h"

#include "CoreTechSurfaceExtension.generated.h"

UCLASS(meta = (DisplayName = "Kernel IO Parametric Surface Data"))
class CORETECHSURFACE_API UCoreTechParametricSurfaceData : public UParametricSurfaceData
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
	UPROPERTY()
	TArray<uint8> RawData_DEPRECATED;

	virtual void Serialize(FArchive& Ar) override;
};
