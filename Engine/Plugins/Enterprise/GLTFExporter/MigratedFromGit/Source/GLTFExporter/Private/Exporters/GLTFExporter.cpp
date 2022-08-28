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

	FormatExtension.Add(TEXT("glb"));
	FormatDescription.Add(TEXT("GL Transmission Format (Binary)"));
}

bool UGLTFExporter::ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Archive, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags)
{
	const UGLTFExportOptions* Options = GetExportOptions();
	if (Options == nullptr)
	{
		// User cancelled the export
		return false;
	}

	FGCObjectScopeGuard OptionsGuard(Options);

	// TODO: add support for UAssetExportTask::IgnoreObjectList?

	FGLTFContainerBuilder Builder(GetFilePath(), Options, bSelectedOnly);
	Builder.ClearLog();

	const bool bSuccess = AddObject(Builder, Object);
	if (bSuccess)
	{
		Builder.Write(Archive, Warn);
	}

	// TODO: should we copy messages to UAssetExportTask::Errors?

	if (!FApp::IsUnattended() && Builder.HasLoggedMessages())
	{
		Builder.OpenLog();
	}

	return bSuccess;
}

bool UGLTFExporter::ExportToGLTF(UObject* Object, const FString& Filename, const UGLTFExportOptions* Options, FGLTFExportMessages& OutMessages)
{
	if (Object == nullptr)
	{
		OutMessages.Errors.Add(TEXT("No object to export"));
		return false;
	}

	UGLTFExporter* Exporter = Cast<UGLTFExporter>(FindExporter(Object, TEXT("gltf")));
	if (Exporter == nullptr)
	{
		OutMessages.Errors.Add(FString::Printf(TEXT("Couldn't find exporter for object of type %s"), *Object->GetClass()->GetName()));
		return false;
	}

	FGLTFContainerBuilder Builder(Filename, Options, false);
	const bool bSuccess = Exporter->AddObject(Builder, Object);

	OutMessages.Suggestions = Builder.GetLoggedSuggestions();
	OutMessages.Warnings = Builder.GetLoggedWarnings();
	OutMessages.Errors = Builder.GetLoggedErrors();

	if (bSuccess)
	{
		FArchive* Archive = IFileManager::Get().CreateFileWriter(*Filename);
		if (Archive == nullptr)
		{
			OutMessages.Errors.Add(FString::Printf(TEXT("Couldn't save file: %s"), *Filename));
			return false;
		}

		Builder.Write(*Archive, nullptr);
		Archive->Close();
	}

	return bSuccess;
}

bool UGLTFExporter::ExportToGLTF(UObject* Object, const FString& Filename, const UGLTFExportOptions* Options)
{
	FGLTFExportMessages Messages;
	return ExportToGLTF(Object, Filename, Options, Messages);
}

bool UGLTFExporter::AddObject(FGLTFContainerBuilder& Builder, const UObject* Object)
{
	return false;
}

const UGLTFExportOptions* UGLTFExporter::GetExportOptions()
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
	}

#if WITH_EDITOR
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
		Options->SaveConfig();
	}
#endif

	return Options;
}

FString UGLTFExporter::GetFilePath() const
{
	return ExportTask != nullptr ? ExportTask->Filename : CurrentFilename;
}
