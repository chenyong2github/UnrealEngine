// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"

#include "InputEditorModule.generated.h"

// Module is not publicly exposed

// Asset factories

UCLASS()
class INPUTEDITOR_API UInputMappingContext_Factory : public UFactory
{
	GENERATED_UCLASS_BODY()
public:
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

UCLASS()
class INPUTEDITOR_API UInputAction_Factory : public UFactory
{
	GENERATED_UCLASS_BODY()
public:
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

// TODO: Add trigger/modifier factories and hook up RegisterAssetTypeActions type construction.
//
//UCLASS()
//class INPUTEDITOR_API UInputTrigger_Factory : public UBlueprintFactory
//{
//	GENERATED_UCLASS_BODY()
//};
//
//UCLASS()
//class INPUTEDITOR_API UInputModifier_Factory : public UBlueprintFactory
//{
//	GENERATED_UCLASS_BODY()
//
//	UPROPERTY(EditAnywhere, Category = DataAsset)
//	TSubclassOf<UDataAsset> DataAssetClass;
//};
