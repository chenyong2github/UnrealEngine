// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "InteractiveToolBuilder.h"

#include "MeshAnalysisProperties.generated.h"


// predeclarations
class FDynamicMesh3;



UCLASS()
class MESHMODELINGTOOLS_API UMeshAnalysisProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	UPROPERTY(VisibleAnywhere, Category = MeshAnalysis)
	FString SurfaceArea;

	UPROPERTY(VisibleAnywhere, Category = MeshAnalysis)
	FString Volume;

	void Update(const FDynamicMesh3& Mesh, const FTransform& Transform);
};

