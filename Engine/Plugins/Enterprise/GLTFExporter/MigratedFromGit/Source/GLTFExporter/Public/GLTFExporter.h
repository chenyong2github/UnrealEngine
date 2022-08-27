// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "Exporters/Exporter.h"
#include "GLTFExporter.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogGLTFExporter, Log, All);

UCLASS()
class GLTFEXPORTER_API UGLTFExporter : public UExporter
{
public:
	GENERATED_BODY()

public:
	UGLTFExporter(const FObjectInitializer& ObjectInitializer = FObjectInitializer());

	//~ Begin UExporter Interface
	virtual bool ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex = 0, uint32 PortFlags = 0) override;
	//~ End UExporter Interface
};
