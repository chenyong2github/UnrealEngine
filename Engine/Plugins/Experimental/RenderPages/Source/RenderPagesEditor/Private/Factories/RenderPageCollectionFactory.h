// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "RenderPage/RenderPagePropsSource.h"
#include "RenderPageCollectionFactory.generated.h"


class URenderPagesBlueprint;
class URenderPageCollection;


/**
 * The factory that creates URenderPageBlueprint (render page collection) instances.
 */
UCLASS(MinimalAPI, HideCategories=Object)
class URenderPagesBlueprintFactory : public UFactory
{
	GENERATED_BODY()

public:
	URenderPagesBlueprintFactory();

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext) override;
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override;
	virtual uint32 GetMenuCategories() const override;

	/** The parent class of the created blueprint. */
	UPROPERTY(EditAnywhere, Category="Render Pages|Render Pages Factory", Meta = (AllowAbstract = ""))
	TSubclassOf<URenderPageCollection> ParentClass;
};
