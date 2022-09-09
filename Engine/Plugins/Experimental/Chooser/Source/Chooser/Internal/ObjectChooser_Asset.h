// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IObjectChooser.h"
#include "ObjectChooser_Asset.generated.h"

UCLASS()
class CHOOSER_API UObjectChooser_Asset : public UObject, public IObjectChooser
{
	GENERATED_BODY()
	
	// IObjectChooser interface
	virtual UObject* ChooseObject(const UObject* ContextObject) const final override;
	
public: 
	UPROPERTY(EditAnywhere, Category = "Parameters")
	TObjectPtr<UObject> Asset;
};
