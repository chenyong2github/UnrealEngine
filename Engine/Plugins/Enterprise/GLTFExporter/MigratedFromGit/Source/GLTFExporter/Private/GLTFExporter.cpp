// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFExporter.h"
#include "UnrealExporter.h"
#include "Engine.h"

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
	ExportOptions = NewObject<UGLTFExportOptions>(this, TEXT("GLTF Export Options"));
}

bool UGLTFExporter::FillExportOptions()
{
	bool ExportAll = GetBatchMode() && !GetShowExportOption();
	bool ExportCancel = false;

	ExportOptions->FillOptions(GetBatchMode(), GetShowExportOption(), UExporter::CurrentFilename, ExportCancel, ExportAll);
	if (ExportCancel)
	{
		SetCancelBatch(GetBatchMode());
		return false;
	}

	SetShowExportOption(!ExportAll);
	return true;
}
