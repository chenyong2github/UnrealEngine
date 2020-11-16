// Copyright Epic Games, Inc. All Rights Reserved.

/**
 *
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "Factories/Factory.h"
#include "BlueprintFactory.generated.h"

class FClassViewerInitializationOptions;

UCLASS(hidecategories=Object, collapsecategories)
class UNREALED_API UBlueprintFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

	// The parent class of the created blueprint
	UPROPERTY(EditAnywhere, Category=BlueprintFactory, meta=(AllowAbstract = "", BlueprintBaseOnly = ""))
	TSubclassOf<class UObject> ParentClass;

	// Skips the class choosing dialog and uses the ParentClass as the blueprint base class
	UPROPERTY()
	bool bSkipClassPicker = false;

	// Delegate allows overriding the class viewer initialization options before displaying it
	DECLARE_DELEGATE_OneParam(FOnConfigureProperties, FClassViewerInitializationOptions*)
	FOnConfigureProperties OnConfigurePropertiesDelegate;

	// UFactory Interface
	virtual bool ConfigureProperties() override;
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext) override;
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	// End of UFactory Interface

protected:
	// Hook for derived classes to indicate if they want to skip some filtering that is unnecessary for macro libraries
	virtual bool IsMacroFactory() const { return false; }
};



