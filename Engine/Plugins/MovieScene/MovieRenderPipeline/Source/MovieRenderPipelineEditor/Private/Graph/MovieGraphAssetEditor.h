// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tools/UAssetEditor.h"

#include "MovieGraphAssetEditor.generated.h"

UCLASS()
class UMovieGraphAssetEditor : public UAssetEditor
{
	GENERATED_BODY()

public:
	//~ Begin UAssetEditor overrides
	virtual TSharedPtr<FBaseAssetToolkit> CreateToolkit() override;
	virtual void GetObjectsToEdit(TArray<UObject*>& InObjectsToEdit) override;
	//~ End UAssetEditor overrides
	
	void SetObjectToEdit(UObject* InObject);

private:
	UPROPERTY(Transient)
	TObjectPtr<UObject> ObjectToEdit;
};