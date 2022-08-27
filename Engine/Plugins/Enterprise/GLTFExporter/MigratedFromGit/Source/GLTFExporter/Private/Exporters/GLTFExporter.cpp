// Copyright Epic Games, Inc. All Rights Reserved.

#include "Exporters/GLTFExporter.h"
#include "UObject/GCObjectScopeGuard.h"
#include "Builders/GLTFContainerBuilder.h"
#include "GLTFExportOptions.h"

UGLTFExporter::UGLTFExporter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = nullptr;
	bText = false;
	PreferredFormatIndex = 0;

	FormatExtension.Add(TEXT("gltf"));
	FormatDescription.Add(TEXT("GL Transmission Format"));

	// TODO: uncomment when support for .glb is implemented
	// FormatExtension.Add(TEXT("glb"));
	// FormatDescription.Add(TEXT("GL Transmission Format (Binary)"));
}

bool UGLTFExporter::ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Archive, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags)
{
	UGLTFExportOptions* Options = NewObject<UGLTFExportOptions>();
	FGCObjectScopeGuard OptionsGuard(Options);

	if (!FillExportOptions(Options))
	{
		// User cancelled the export
		return false;
	}

	bool bSuccess = true;

	// TODO: add support for UAssetExportTask::IgnoreObjectList?

	FGLTFContainerBuilder Builder(Options, bSelectedOnly);
	if (AddObject(Builder, Object))
	{
		if (!Builder.Serialize(Archive, CurrentFilename))
		{
			// TODO: more descriptive error
			Builder.AddErrorMessage(TEXT("Serialize failed"));
			bSuccess = false;
		}
	}
	else
	{
		// TODO: more descriptive error
		Builder.AddErrorMessage(TEXT("AddObject failed"));
		bSuccess = false;
	}

	// TODO: should we copy messages to UAssetExportTask::Errors?

	if (FApp::IsUnattended())
	{
		Builder.WriteLogMessagesToConsole();
	}
	else
	{
		Builder.ShowLogMessages();
	}

	return bSuccess;
}

bool UGLTFExporter::AddObject(FGLTFContainerBuilder& Builder, const UObject* Object)
{
	return false;
}

bool UGLTFExporter::FillExportOptions(UGLTFExportOptions* ExportOptions)
{
	bool bExportAll = GetBatchMode() && !GetShowExportOption();
	bool bExportCancel = false;

	ExportOptions->FillOptions(GetBatchMode(), GetShowExportOption(), CurrentFilename, bExportCancel, bExportAll);
	if (bExportCancel)
	{
		SetCancelBatch(GetBatchMode());
		return false;
	}

	SetShowExportOption(!bExportAll);
	return true;
}
