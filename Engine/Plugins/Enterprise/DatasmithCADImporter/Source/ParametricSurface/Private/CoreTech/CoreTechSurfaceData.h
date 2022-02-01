// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ParametricSurfaceData.h"
#include "DatasmithAdditionalData.h"
#include "DatasmithCustomAction.h"
#include "DatasmithImportOptions.h"
#include "DatasmithUtils.h"

#include "CoreTechSurfaceData.generated.h"

UCLASS(meta = (DisplayName = "Kernel IO Parametric Surface Data"))
class PARAMETRICSURFACE_API UCoreTechParametricSurfaceData : public UParametricSurfaceData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FString SourceFile;

	virtual bool SetFile(const TCHAR* FilePath) override;

	virtual bool Tessellate(UStaticMesh& StaticMesh, const FDatasmithRetessellationOptions& RetessellateOptions) override;
};
