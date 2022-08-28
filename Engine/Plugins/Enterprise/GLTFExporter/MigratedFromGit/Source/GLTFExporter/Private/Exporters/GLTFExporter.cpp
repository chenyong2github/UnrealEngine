// Copyright Epic Games, Inc. All Rights Reserved.

#include "Exporters/GLTFExporter.h"
#include "GLTFExportOptions.h"
#include "UI/GLTFExportOptionsWindow.h"
#include "Builders/GLTFContainerBuilder.h"
#include "UObject/GCObjectScopeGuard.h"
#include "AssetExportTask.h"

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
	UGLTFExportOptions* Options = GetExportOptions();
	if (Options == nullptr)
	{
		// User cancelled the export
		return false;
	}

	FGCObjectScopeGuard OptionsGuard(Options);
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

UGLTFExportOptions* UGLTFExporter::GetExportOptions()
{
	UGLTFExportOptions* Options = nullptr;
	bool bAutomatedTask = GIsAutomationTesting || FApp::IsUnattended();

	if (ExportTask != nullptr)
	{
		Options = Cast<UGLTFExportOptions>(ExportTask->Options);

		if (ExportTask->bAutomated)
		{
			bAutomatedTask = true;
		}
	}

	if (Options == nullptr)
	{
		Options = NewObject<UGLTFExportOptions>();
		Options->LoadOptions();
	}

	if (GetShowExportOption() && !bAutomatedTask)
	{
		bool bExportAll = GetBatchMode();
		bool bOperationCanceled = false;

		FGCObjectScopeGuard OptionsGuard(Options);
		SGLTFExportOptionsWindow::ShowDialog(Options, CurrentFilename, GetBatchMode(), bOperationCanceled, bExportAll);

		if (bOperationCanceled)
		{
			SetCancelBatch(GetBatchMode());
			return nullptr;
		}

		SetShowExportOption(!bExportAll);
		Options->SaveOptions();
	}

	return Options;
}
