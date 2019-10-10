// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Properties/MeshAnalysisProperties.h"

#include "DynamicMesh3.h"
#include "MeshQueries.h"
#include "MeshAdapter.h"
#include "MeshAdapterUtil.h"

#define LOCTEXT_NAMESPACE "UMeshAnalysisProperites"


void UMeshAnalysisProperties::Update(const FDynamicMesh3& MeshIn, const FTransform& Transform)
{
	FTriangleMeshAdapterd TransformedMesh = MeshAdapterUtil::MakeTransformedDynamicMeshAdapter(&MeshIn, Transform);
	FVector2d VolArea = TMeshQueries<FTriangleMeshAdapterd>::GetVolumeArea(TransformedMesh);
	this->SurfaceArea = FString::Printf(TEXT("%.2f m^2"), VolArea.Y / 10000);
	this->Volume = FString::Printf(TEXT("%.2f m^3"), VolArea.X / 1000000);
}



#undef LOCTEXT_NAMESPACE
