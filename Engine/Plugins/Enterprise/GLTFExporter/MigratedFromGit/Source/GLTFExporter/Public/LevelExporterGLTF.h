// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "Exporters/Exporter.h"
#include "LevelExporterGLTF.generated.h"

class FExportObjectInnerContext;
class UActorComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogGLTFExporter, Log, All);

UCLASS()
class ULevelExporterGLTF : public UExporter
{
public:
	GENERATED_BODY()

public:
	ULevelExporterGLTF(const FObjectInitializer& ObjectInitializer = FObjectInitializer());

	//~ Begin UExporter Interface

	virtual bool SupportsObject(UObject* Object) const override;

	virtual bool ExportText(const FExportObjectInnerContext* Context, UObject* Object, const TCHAR* Type, FOutputDevice& Ar, FFeedbackContext* Warn, uint32 PortFlags = 0) override;
	virtual bool ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex = 0, uint32 PortFlags = 0) override;
	virtual void ExportComponentExtra(const FExportObjectInnerContext* Context, const TArray<UActorComponent*>& Components, FOutputDevice& Ar, uint32 PortFlags) override;

	virtual void ExportPackageObject(FExportPackageParams& ExpPackageParams) override;
	virtual void ExportPackageInners(FExportPackageParams& ExpPackageParams) override;

	virtual int32 GetFileCount(void) const override;
	virtual FString GetUniqueFilename(const TCHAR* Filename, int32 FileIndex) override;

	virtual bool GetBatchMode() const override;
	virtual void SetBatchMode(bool InBatchExportMode) override;
	virtual bool GetCancelBatch() const override;
	virtual void SetCancelBatch(bool InCancelBatch) override;

	virtual bool GetShowExportOption() const override;
	virtual void SetShowExportOption(bool InShowExportOption) override;

	//~ End UExporter Interface
};
