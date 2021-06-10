// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DynamicMesh/DynamicMeshAttributeSet.h" //FDynamicMeshUVOverlay
#include "VectorTypes.h"

#include "GeometryBase.h"

PREDECLARE_GEOMETRY(class FDynamicMesh3);

namespace UE {
namespace Geometry {
namespace UVEditorToolUtil {

	UVEDITORTOOLS_API void GenerateUVUnwrapMesh(const FDynamicMeshUVOverlay& UVOverlay, FDynamicMesh3& UnwrapMeshOut,
		TFunctionRef<FVector3d(const FVector2f&)> UVToVertPosition);

	UVEDITORTOOLS_API void UpdateUVUnwrapMesh(const FDynamicMeshUVOverlay& UVOverlayIn,
		FDynamicMesh3& UnwrapMeshOut, TFunctionRef<FVector3d(const FVector2f&)> UVToVertPosition,
		const TArray<int32>* ChangedElementIDs, const TArray<int32>* ChangedTids);

	UVEDITORTOOLS_API void UpdateUVOverlayFromUnwrapMesh(
		const FDynamicMesh3& UnwrapMeshIn, FDynamicMeshUVOverlay& UVOverlayOut,
		TFunctionRef<FVector2f(const FVector3d&)> UVToVertPosition,
		const TArray<int32>* ChangedVids = nullptr, const TArray<int32>* ChangedTids = nullptr);

}}}