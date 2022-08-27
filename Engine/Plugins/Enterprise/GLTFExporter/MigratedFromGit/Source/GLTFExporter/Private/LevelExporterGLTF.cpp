// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelExporterGLTF.h"
#include "UnrealExporter.h"
#include "AssetExportTask.h"
#include "Engine/World.h"
#include "Engine.h"

DEFINE_LOG_CATEGORY(LogGLTFExporter);

ULevelExporterGLTF::ULevelExporterGLTF(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UWorld::StaticClass();
	bText = false;
	PreferredFormatIndex = 0;
	FormatExtension.Add(TEXT("gltf"));
	FormatExtension.Add(TEXT("glb"));
	FormatDescription.Add(TEXT("GL Transmission Format"));
	FormatDescription.Add(TEXT("GL Transmission Format (Binary)"));
}

bool ULevelExporterGLTF::ExportText(const FExportObjectInnerContext* Context, UObject* Object, const TCHAR* Type, FOutputDevice& Ar, FFeedbackContext* Warn, uint32 PortFlags)
{
	UE_LOG(LogGLTFExporter, Warning, TEXT("ULevelExporterGLTF::ExportText"));
	UE_LOG(LogGLTFExporter, Warning, TEXT("Object: %s (%s)"), *(Object->GetName()), *(Object->GetClass()->GetName()));
	UE_LOG(LogGLTFExporter, Warning, TEXT("Type: %s"), Type);
	UE_LOG(LogGLTFExporter, Warning, TEXT("PortFlags: %d"), PortFlags);
	return true;
}

bool ULevelExporterGLTF::ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags)
{
	UE_LOG(LogGLTFExporter, Warning, TEXT("ULevelExporterGLTF::ExportBinary"));
	UE_LOG(LogGLTFExporter, Warning, TEXT("Object: %s (%s)"), *(Object->GetName()), *(Object->GetClass()->GetName()));
	UE_LOG(LogGLTFExporter, Warning, TEXT("Type: %s"), Type);
	UE_LOG(LogGLTFExporter, Warning, TEXT("FileIndex: %d"), FileIndex);
	UE_LOG(LogGLTFExporter, Warning, TEXT("PortFlags: %d"), PortFlags);
	return true;
}

void ULevelExporterGLTF::ExportComponentExtra(const FExportObjectInnerContext* Context, const TArray<UActorComponent*>& Components, FOutputDevice& Ar, uint32 PortFlags)
{
	UE_LOG(LogGLTFExporter, Warning, TEXT("ULevelExporterGLTF::ExportComponentExtra"));
	UE_LOG(LogGLTFExporter, Warning, TEXT("Components: %d"), Components.Num());
	for (int32 i = 0; i != Components.Num(); ++i)
	{
		auto& Component = Components[i];
		UE_LOG(LogGLTFExporter, Warning, TEXT("Components[%d]: %s (%s)"), i, *(Component->GetName()), *(Component->GetClass()->GetName()));
	}
	UE_LOG(LogGLTFExporter, Warning, TEXT("PortFlags: %d"), PortFlags);
}

void ULevelExporterGLTF::ExportPackageObject(FExportPackageParams& ExpPackageParams)
{
	UE_LOG(LogGLTFExporter, Warning, TEXT("ULevelExporterGLTF::ExportPackageObject"));
	UE_LOG(LogGLTFExporter, Warning, TEXT("ExpPackageParams.Type: %s"), ExpPackageParams.Type);
	UE_LOG(LogGLTFExporter, Warning, TEXT("ExpPackageParams.RootMapPackageName: %d"), *(ExpPackageParams.RootMapPackageName));
	UE_LOG(LogGLTFExporter, Warning, TEXT("PortFlags: %d"), ExpPackageParams.PortFlags);
}

void ULevelExporterGLTF::ExportPackageInners(FExportPackageParams& ExpPackageParams)
{
	UE_LOG(LogGLTFExporter, Warning, TEXT("ULevelExporterGLTF::ExportPackageInners"));
	UE_LOG(LogGLTFExporter, Warning, TEXT("ExpPackageParams.Type: %s"), ExpPackageParams.Type);
	UE_LOG(LogGLTFExporter, Warning, TEXT("ExpPackageParams.RootMapPackageName: %d"), *(ExpPackageParams.RootMapPackageName));
	UE_LOG(LogGLTFExporter, Warning, TEXT("PortFlags: %d"), ExpPackageParams.PortFlags);
}

bool ULevelExporterGLTF::SupportsObject(UObject* Object) const
{
	UE_LOG(LogGLTFExporter, Warning, TEXT("ULevelExporterGLTF::SupportsObject"));
	UE_LOG(LogGLTFExporter, Warning, TEXT("Object: %s (%s)"), *(Object->GetName()), *(Object->GetClass()->GetName()));
	return UExporter::SupportsObject(Object);
}

int32 ULevelExporterGLTF::GetFileCount() const
{
	UE_LOG(LogGLTFExporter, Warning, TEXT("ULevelExporterGLTF::GetFileCount"));
	return UExporter::GetFileCount();
}

FString ULevelExporterGLTF::GetUniqueFilename(const TCHAR * Filename, int32 FileIndex)
{
	UE_LOG(LogGLTFExporter, Warning, TEXT("ULevelExporterGLTF::GetUniqueFilename"));
	UE_LOG(LogGLTFExporter, Warning, TEXT("Filename: %s"), Filename);
	UE_LOG(LogGLTFExporter, Warning, TEXT("FileIndex: %d"), FileIndex);
	return UExporter::GetUniqueFilename(Filename, FileIndex);
}

bool ULevelExporterGLTF::GetBatchMode() const
{
	UE_LOG(LogGLTFExporter, Warning, TEXT("ULevelExporterGLTF::GetBatchMode"));
	return UExporter::GetBatchMode();
}

void ULevelExporterGLTF::SetBatchMode(bool InBatchExportMode)
{
	UE_LOG(LogGLTFExporter, Warning, TEXT("ULevelExporterGLTF::SetBatchMode"));
	UE_LOG(LogGLTFExporter, Warning, TEXT("InBatchExportMode: %s"), (InBatchExportMode ? TEXT("true") : TEXT("false")));
	UExporter::SetBatchMode(InBatchExportMode);
}

bool ULevelExporterGLTF::GetCancelBatch() const
{
	UE_LOG(LogGLTFExporter, Warning, TEXT("ULevelExporterGLTF::GetCancelBatch"));
	return UExporter::GetCancelBatch();
}

void ULevelExporterGLTF::SetCancelBatch(bool InCancelBatch)
{
	UE_LOG(LogGLTFExporter, Warning, TEXT("ULevelExporterGLTF::SetCancelBatch"));
	UE_LOG(LogGLTFExporter, Warning, TEXT("InCancelBatch: %s"), (InCancelBatch ? TEXT("true") : TEXT("false")));
	UExporter::SetCancelBatch(InCancelBatch);
}

bool ULevelExporterGLTF::GetShowExportOption() const
{
	UE_LOG(LogGLTFExporter, Warning, TEXT("ULevelExporterGLTF::GetShowExportOption"));
	return UExporter::GetShowExportOption();
}

void ULevelExporterGLTF::SetShowExportOption(bool InShowExportOption)
{
	UE_LOG(LogGLTFExporter, Warning, TEXT("ULevelExporterGLTF::SetShowExportOption"));
	UE_LOG(LogGLTFExporter, Warning, TEXT("InShowExportOption: %s"), (InShowExportOption ? TEXT("true") : TEXT("false")));
	UExporter::SetShowExportOption(InShowExportOption);
}
