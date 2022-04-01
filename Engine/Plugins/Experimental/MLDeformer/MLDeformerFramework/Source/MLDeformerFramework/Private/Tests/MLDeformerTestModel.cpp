// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerTestModel.h"

namespace UE::MLDeformerTests
{
	FTestModelInstance::FTestModelInstance(UMLDeformerModel* InModel)
		: FMLDeformerModelInstance(InModel)
	{
	}
}	// namespace UE::MLDeformerTests

UE::MLDeformer::FMLDeformerModelInstance* UMLDeformerTestModel::CreateModelInstance()
{
	return new UE::MLDeformerTests::FTestModelInstance(this);
}

FString UMLDeformerTestModel::GetDisplayName() const 
{ 
	return FString("Test Model");
}

void UMLDeformerTestModel::UpdateNumTargetMeshVertices()
{
	NumTargetMeshVerts = NumBaseMeshVerts;
}
