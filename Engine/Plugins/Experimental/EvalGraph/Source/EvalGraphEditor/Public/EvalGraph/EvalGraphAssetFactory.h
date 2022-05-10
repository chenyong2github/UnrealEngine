// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"

#include "EvalGraphAssetFactory.generated.h"

UCLASS()
class UEvalGraphAssetFactory : public UFactory
{
	GENERATED_BODY()
public:

	UEvalGraphAssetFactory();

	/** UFactory Interface */
	bool     CanCreateNew() const override;
	bool     FactoryCanImport(const FString& Filename) override;
	UObject* FactoryCreateNew(UClass*           InClass,
							  UObject*          InParent,
							  FName             InName,
							  EObjectFlags      Flags,
							  UObject*          Context,
							  FFeedbackContext* Warn) override;

	bool ShouldShowInNewMenu() const override;
	bool ConfigureProperties() override;
	/** End UFactory Interface */

private:
};
