// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "Exporters/Exporter.h"
#include "GLTFExporter.generated.h"

class FGLTFContainerBuilder;
class UGLTFExportOptions;

UCLASS()
class GLTFEXPORTER_API UGLTFExporter : public UExporter
{
public:

	GENERATED_BODY()

	explicit UGLTFExporter(const FObjectInitializer& ObjectInitializer);

	virtual bool ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Archive, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags) override;

protected:

	virtual bool Add(FGLTFContainerBuilder& Builder, const UObject* Object);

private:

	bool FillExportOptions(UGLTFExportOptions* ExportOptions);
};
