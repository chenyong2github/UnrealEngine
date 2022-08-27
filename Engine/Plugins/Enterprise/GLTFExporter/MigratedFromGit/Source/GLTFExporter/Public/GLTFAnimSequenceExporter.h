// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFExporter.h"
#include "GLTFAnimSequenceExporter.generated.h"

UCLASS()
class UGLTFAnimSequenceExporter : public UGLTFExporter
{
public:
	GENERATED_BODY()

public:
	UGLTFAnimSequenceExporter(const FObjectInitializer& ObjectInitializer = FObjectInitializer());

	//~ Begin UExporter Interface
	virtual bool ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex = 0, uint32 PortFlags = 0) override;
	//~ End UExporter Interface
};
