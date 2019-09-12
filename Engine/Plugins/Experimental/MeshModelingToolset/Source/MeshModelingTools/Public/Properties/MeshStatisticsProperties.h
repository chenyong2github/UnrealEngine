// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	int TriangleCount = 0;

	UPROPERTY(VisibleAnywhere, Category = MeshStatistics)
	int VertexCount = 0;

	void Update(const FDynamicMesh3& Mesh);
};

