// Copyright Epic Games, Inc. All Rights Reserved.

#include "Exporters/GLTFAnimSequenceExporter.h"
#include "Animation/AnimSequence.h"

UGLTFAnimSequenceExporter::UGLTFAnimSequenceExporter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UAnimSequence::StaticClass();
}

bool UGLTFAnimSequenceExporter::ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Archive, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags)
{
	const UAnimSequence* AnimSequence = CastChecked<UAnimSequence>(Object);

	if (!FillExportOptions())
	{
		// User cancelled the export
		return false;
	}

	// TODO: implement
	return true;
}
