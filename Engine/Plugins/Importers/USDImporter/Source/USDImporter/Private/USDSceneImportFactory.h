// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/SceneImportFactory.h"
#include "Editor/EditorEngine.h"
#include "Factories/ImportSettings.h"
#include "USDImporter.h"

#include "USDSceneImportFactory.generated.h"

class UDEPRECATED_UUSDSceneImportOptions;
class UWorld;


UCLASS(transient, Deprecated)
class UDEPRECATED_UUSDSceneImportFactory : public USceneImportFactory, public IImportSettingsParser
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

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use the new USDStageImporter module instead"))
	UDEPRECATED_UUSDSceneImportOptions* ImportOptions_DEPRECATED;
};

