// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Factories/Factory.h"
#include "EditorReimportHandler.h"

#include "DisplayClusterConfiguratorFactory.generated.h"

UCLASS(MinimalApi)
class UDisplayClusterConfiguratorFactory
	: public UFactory
{
	GENERATED_BODY()

public:
	UDisplayClusterConfiguratorFactory();

	//~ Begin UFactory Interface
	virtual bool DoesSupportClass(UClass* Class) override;
	virtual UClass* ResolveSupportedClass() override;
	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& InFilename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;
	virtual bool FactoryCanImport(const FString& Filename) override { return true; }
	//~ End UFactory Interface
};

UCLASS(MinimalApi)
class UDisplayClusterConfiguratorReimportFactory
	: public UDisplayClusterConfiguratorFactory
	, public FReimportHandler
{
	GENERATED_BODY()

	UDisplayClusterConfiguratorReimportFactory();

	//~ Begin FReimportHandler Interface
	virtual bool CanReimport(UObject* Obj, TArray<FString>& OutFilenames) override;
	virtual void SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths) override;
	virtual EReimportResult::Type Reimport(UObject* Obj) override;
	virtual int32 GetPriority() const override;
	//~ End FReimportHandler Interface
};
