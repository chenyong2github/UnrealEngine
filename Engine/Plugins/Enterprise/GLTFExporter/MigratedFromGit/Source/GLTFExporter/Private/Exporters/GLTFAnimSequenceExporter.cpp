// Copyright Epic Games, Inc. All Rights Reserved.

#include "Exporters/GLTFAnimSequenceExporter.h"
#include "Animation/AnimSequence.h"

UGLTFAnimSequenceExporter::UGLTFAnimSequenceExporter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// TODO: uncomment when support is implemented
	// SupportedClass = UAnimSequence::StaticClass();
}

bool UGLTFAnimSequenceExporter::Add(FGLTFContainerBuilder& Builder, const UObject* Object)
{
	const UAnimSequence* AnimSequence = CastChecked<UAnimSequence>(Object);
	// TODO: implement
	return true;
}
