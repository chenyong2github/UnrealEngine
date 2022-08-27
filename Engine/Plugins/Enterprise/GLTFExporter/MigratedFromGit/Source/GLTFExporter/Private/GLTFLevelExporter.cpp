// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFLevelExporter.h"
#include "Engine/World.h"

UGLTFLevelExporter::UGLTFLevelExporter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UWorld::StaticClass();
}

bool UGLTFLevelExporter::ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags)
{
	UWorld* World = CastChecked<UWorld>(Object);
	return Super::ExportBinary(World, Type, Ar, Warn, FileIndex, PortFlags);
}
