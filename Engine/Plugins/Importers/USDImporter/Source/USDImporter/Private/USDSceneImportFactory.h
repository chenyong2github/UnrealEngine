// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Factories/SceneImportFactory.h"
#include "Editor/EditorEngine.h"
#include "Factories/ImportSettings.h"
#include "USDImporter.h"

#include "USDSceneImportFactory.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUSDSceneImport, Log, All);

class UUSDSceneImportOptions;
class UWorld;


UCLASS(transient)
class UUSDSceneImportFactory : public USceneImportFactory, public IImportSettingsParser
{
	GENERATED_UCLASS_BODY()

public:
	// UFactory Interface
	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;
	virtual bool FactoryCanImport(const FString& Filename) override;
	virtual void CleanUp() override;
	virtual IImportSettingsParser* GetImportSettingsParser() override { return this; }

	// IImportSettingsParser interface
	virtual void ParseFromJson(TSharedRef<class FJsonObject> ImportSettingsJson) override;

private:
	UPROPERTY()
	FUSDSceneImportContext ImportContext;

	UPROPERTY()
	UUSDSceneImportOptions* ImportOptions;
};

