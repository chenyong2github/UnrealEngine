// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "USDImporter.h"
#include "Factories/ImportSettings.h"
#include "EditorReimportHandler.h"
#include "USDAssetImportFactory.generated.h"


USTRUCT()
struct FUSDAssetImportContext : public FUsdImportContext
{
	GENERATED_USTRUCT_BODY()

#if USE_USD_SDK
	virtual void Init(UObject* InParent, const FString& InName, const UE::FUsdStage& InStage) override;
#endif // #if USE_USD_SDK
};

UCLASS(transient, Deprecated)
class UDEPRECATED_UUSDAssetImportFactory : public UFactory, public IImportSettingsParser, public FReimportHandler
{
	GENERATED_UCLASS_BODY()

public:
	/** UFactory interface */
	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;
	virtual bool FactoryCanImport(const FString& Filename) override;
	virtual void CleanUp() override;
	virtual IImportSettingsParser* GetImportSettingsParser() override { return this; }

	/** FReimportHandler interface */
	virtual bool CanReimport(UObject* Obj, TArray<FString>& OutFilenames);
	virtual void SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths);
	virtual EReimportResult::Type Reimport(UObject* Obj);

	/** IImportSettingsParser interface */
	virtual void ParseFromJson(TSharedRef<class FJsonObject> ImportSettingsJson) override;
private:
	UPROPERTY()
	FUSDAssetImportContext ImportContext;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use the new USDStageImporter module instead"))
	UDEPRECATED_UUSDImportOptions* ImportOptions_DEPRECATED;
};
