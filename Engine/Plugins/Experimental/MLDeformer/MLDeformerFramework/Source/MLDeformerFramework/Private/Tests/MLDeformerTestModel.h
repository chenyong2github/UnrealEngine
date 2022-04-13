// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerModel.h"
#include "MLDeformerModelInstance.h"
#include "MLDeformerTestModel.generated.h"

class UMLDeformerComponent;

UCLASS()
class UMLDeformerTestModel 
	: public UMLDeformerModel
{
	GENERATED_BODY()

public:
	UMLDeformerModelInstance* CreateModelInstance(UMLDeformerComponent* Component) override;
	FString GetDisplayName() const override;
	void UpdateNumTargetMeshVertices() override;
};

UCLASS()
class UTestModelInstance
	: public UMLDeformerModelInstance
{
	GENERATED_BODY()

public:
	void Tick(float DeltaTime) {}
};
