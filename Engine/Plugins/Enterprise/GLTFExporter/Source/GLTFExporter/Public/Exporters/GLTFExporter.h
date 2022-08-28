// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Exporters/Exporter.h"
#include "GLTFExporter.generated.h"

class FGLTFContainerBuilder;
class UGLTFExportOptions;

USTRUCT(BlueprintType)
struct FGLTFExportMessages
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = General)
	TArray<FString> Suggestions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = General)
	TArray<FString> Warnings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = General)
	TArray<FString> Errors;
};

UCLASS(Abstract)
class GLTFEXPORTER_API UGLTFExporter : public UExporter
{
public:

	GENERATED_BODY()

	explicit UGLTFExporter(const FObjectInitializer& ObjectInitializer);

	virtual bool ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Archive, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags) override;

	UFUNCTION(BlueprintCallable, Category = "Miscellaneous")
	static bool ExportToGLTF(UObject* Object, const FString& FilePath, const UGLTFExportOptions* Options, const TSet<AActor*>& SelectedActors, FGLTFExportMessages& OutMessages);
	static bool ExportToGLTF(UObject* Object, const FString& FilePath, const UGLTFExportOptions* Options = nullptr, const TSet<AActor*>& SelectedActors = {});

protected:

	virtual bool AddObject(FGLTFContainerBuilder& Builder, const UObject* Object);

private:

	const UGLTFExportOptions* GetExportOptions();
	FString GetFilePath() const;
};
