// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "InteractiveToolBuilder.h"

#include "MeshStatisticsProperties.generated.h"


// predeclarations
class FDynamicMesh3;



UCLASS()
class MESHMODELINGTOOLS_API UMeshStatisticsProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	UPROPERTY(VisibleAnywhere, Category = MeshStatistics)
	FString Mesh;

	UPROPERTY(VisibleAnywhere, Category = MeshStatistics, AdvancedDisplay)
	FString UV;

	UPROPERTY(VisibleAnywhere, Category = MeshStatistics, AdvancedDisplay)
	FString Attributes;

	void Update(const FDynamicMesh3& Mesh);
};

