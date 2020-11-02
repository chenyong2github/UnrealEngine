// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"

#include "VCamModifierFactory.generated.h"

UCLASS(hidecategories = Object)
class UVCamModifierFactory : public UFactory
{
	GENERATED_BODY()
public:
	UVCamModifierFactory();
	
	// The parent class of the created blueprint
    UPROPERTY(EditAnywhere, Category="BlueprintVirtualSubjectFactory", meta=(AllowAbstract = "", BlueprintBaseOnly = ""))
    TSubclassOf<UObject> ParentClass;
	
	//~ Begin UFactory Interface
	virtual FText GetDisplayName() const override;
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual uint32 GetMenuCategories() const override;
	//~ Begin UFactory Interface
	
};