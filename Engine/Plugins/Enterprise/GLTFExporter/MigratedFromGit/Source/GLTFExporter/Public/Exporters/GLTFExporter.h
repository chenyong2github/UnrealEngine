// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "Exporters/Exporter.h"
#include "GLTFExportOptions.h"
#include "GLTFExporter.generated.h"

UCLASS()
class GLTFEXPORTER_API UGLTFExporter : public UExporter
{
public:
	GENERATED_BODY()

public:
	UGLTFExporter(const FObjectInitializer& ObjectInitializer = FObjectInitializer());

protected:

	bool FillExportOptions();

	UPROPERTY(transient)
	UGLTFExportOptions* ExportOptions;
};
