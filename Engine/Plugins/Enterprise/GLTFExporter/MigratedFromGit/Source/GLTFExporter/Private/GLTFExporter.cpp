// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFExporter.h"
#include "UnrealExporter.h"
#include "Engine.h"

DEFINE_LOG_CATEGORY(LogGLTFExporter);

UGLTFExporter::UGLTFExporter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = nullptr;
	bText = false;
	PreferredFormatIndex = 0;
	FormatExtension.Add(TEXT("gltf"));
	FormatExtension.Add(TEXT("glb"));
	FormatDescription.Add(TEXT("GL Transmission Format"));
	FormatDescription.Add(TEXT("GL Transmission Format (Binary)"));
}

bool UGLTFExporter::ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags)
{
	UE_LOG(LogGLTFExporter, Warning, TEXT("%s::ExportBinary"), *(this->GetClass()->GetName()));
	UE_LOG(LogGLTFExporter, Warning, TEXT("Object: %s (%s)"), *(Object->GetName()), *(Object->GetClass()->GetName()));
	UE_LOG(LogGLTFExporter, Warning, TEXT("Type: %s"), Type);
	UE_LOG(LogGLTFExporter, Warning, TEXT("FileIndex: %d"), FileIndex);
	UE_LOG(LogGLTFExporter, Warning, TEXT("PortFlags: %d"), PortFlags);
	return true;
}
