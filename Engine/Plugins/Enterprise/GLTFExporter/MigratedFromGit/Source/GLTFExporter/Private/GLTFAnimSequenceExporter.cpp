// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFAnimSequenceExporter.h"
#include "Animation/AnimSequence.h"

UGLTFAnimSequenceExporter::UGLTFAnimSequenceExporter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UAnimSequence::StaticClass();
}

bool UGLTFAnimSequenceExporter::ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags)
{
	UAnimSequence* AnimSequence = CastChecked<UAnimSequence>(Object);
	return Super::ExportBinary(AnimSequence, Type, Ar, Warn, FileIndex, PortFlags);
}
