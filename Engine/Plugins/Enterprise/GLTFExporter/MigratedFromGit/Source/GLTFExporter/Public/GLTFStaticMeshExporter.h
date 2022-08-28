// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFExporter.h"
#include "GLTFStaticMeshExporter.generated.h"

UCLASS()
class GLTFEXPORTER_API UGLTFStaticMeshExporter : public UGLTFExporter
{
public:
	GENERATED_BODY()

public:
	UGLTFStaticMeshExporter(const FObjectInitializer& ObjectInitializer = FObjectInitializer());

	//~ Begin UExporter Interface
	virtual bool ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex = 0, uint32 PortFlags = 0) override;
	//~ End UExporter Interface
};
