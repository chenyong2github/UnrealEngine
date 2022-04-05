// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MLDeformerAsset.generated.h"

class UMLDeformerModel;

/**
 * The machine learning deformer asset class.
 * This class contains a Model property, through which all the magic happens.
 */
UCLASS(BlueprintType, hidecategories=Object)
class MLDEFORMERFRAMEWORK_API UMLDeformerAsset 
	: public UObject
{
	GENERATED_BODY()

public:
	UMLDeformerModel* GetModel() const { return Model.Get(); }
	void SetModel(UMLDeformerModel* InModel) { Model = InModel; }

public:
	/** The ML Deformer model, used to deform the mesh. */
	UPROPERTY()
	TObjectPtr<UMLDeformerModel> Model;
};
