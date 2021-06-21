// Copyright Epic Games, Inc. All Rights Reserved.

#include "UncontrolledChangelistValidator.h"

#include "DataValidationChangelist.h"
#include "UncontrolledChangelistsModule.h"

#define LOCTEXT_NAMESPACE "UncontrolledChangelistValidation"

bool UUncontrolledChangelistValidator::CanValidateAsset_Implementation(UObject* InAsset) const
{
	return (InAsset != nullptr) && (UDataValidationChangelist::StaticClass() == InAsset->GetClass());
}

EDataValidationResult UUncontrolledChangelistValidator::ValidateLoadedAsset_Implementation(UObject* InAsset, TArray<FText>& ValidationErrors)
{
	FUncontrolledChangelistsModule& UncontrolledChangelistModule = FUncontrolledChangelistsModule::Get();

	if (UncontrolledChangelistModule.OnReconcileAssets())
	{
		AssetFails(InAsset, LOCTEXT("UncontrolledChangesFound", "Uncontrolled changes found, please verify if they should be added to your changelist."), ValidationErrors);
		return EDataValidationResult::NotValidated;
	}

	AssetPasses(InAsset);
	return EDataValidationResult::Valid;
}

#undef LOCTEXT_NAMESPACE
