// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MLDeformerModel.h"
#include "MLDeformerModelInstance.h"
#include "MLDeformerTestModel.generated.h"

UCLASS()
class UMLDeformerTestModel 
	:  public UMLDeformerModel
{
	GENERATED_BODY()

public:
	UE::MLDeformer::FMLDeformerModelInstance* CreateModelInstance() override;
	FString GetDisplayName() const override;
	void UpdateNumTargetMeshVertices() override;
};

namespace UE::MLDeformerTests
{
	using namespace UE::MLDeformer;

	class FTestModelInstance
		: public FMLDeformerModelInstance
	{
	public:
		FTestModelInstance(UMLDeformerModel* InModel);
		void Tick(float DeltaTime) {}
	};
}	// namespace UE::MLDeformerTests
