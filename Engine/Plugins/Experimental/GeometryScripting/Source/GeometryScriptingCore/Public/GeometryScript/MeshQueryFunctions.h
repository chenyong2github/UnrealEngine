// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "MeshQueryFunctions.generated.h"

class UDynamicMesh;


UCLASS(meta = (ScriptName = "GeometryScript_MeshQueries"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_MeshQueryFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries")
	static bool GetIsDenseMesh( UDynamicMesh* TargetMesh );

	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries")
	static bool GetMeshHasAttributeSet( UDynamicMesh* TargetMesh );

	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries")
	static UPARAM(DisplayName = "Bounding Box") FBox GetMeshBoundingBox( UDynamicMesh* TargetMesh );

	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries")
	static void GetMeshVolumeArea( UDynamicMesh* TargetMesh, float& SurfaceArea, float& Volume );

	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries")
	static bool GetIsClosedMesh( UDynamicMesh* TargetMesh );

	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries")
	static UPARAM(DisplayName = "Num Loops") int32 GetNumOpenBorderLoops( UDynamicMesh* TargetMesh, bool& bAmbiguousTopologyFound );

	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries")
	static UPARAM(DisplayName = "Num Edges") int32 GetNumOpenBorderEdges( UDynamicMesh* TargetMesh );



	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries")
	static UPARAM(DisplayName = "Triangle Count") int32 GetTriangleCount( UDynamicMesh* TargetMesh );

	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries")
	static UPARAM(DisplayName = "Num Triangle IDs") int32 GetNumTriangleIDs( UDynamicMesh* TargetMesh );

	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries")
	static bool GetHasTriangleIDGaps( UDynamicMesh* TargetMesh );

	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries")
	static bool IsValidTriangleID( UDynamicMesh* TargetMesh, int32 TriangleID );

	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries")
	static UPARAM(DisplayName = "Indices") FIntVector GetTriangleIndices( UDynamicMesh* TargetMesh, int32 TriangleID, bool& bIsValidTriangle );

	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries")
	static void GetTrianglePositions( UDynamicMesh* TargetMesh, int32 TriangleID, bool& bIsValidTriangle, FVector& Vertex1, FVector& Vertex2, FVector& Vertex3 );

	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries")
	static UPARAM(DisplayName = "Normal") FVector GetTriangleFaceNormal( UDynamicMesh* TargetMesh, int32 TriangleID, bool& bIsValidTriangle );




	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries")
	static UPARAM(DisplayName = "Vertex Count") int32 GetVertexCount( UDynamicMesh* TargetMesh );

	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries")
	static UPARAM(DisplayName = "Num Vertex IDs") int32 GetNumVertexIDs( UDynamicMesh* TargetMesh );

	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries")
	static bool GetHasVertexIDGaps( UDynamicMesh* TargetMesh );

	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries")
	static bool IsValidVertexID( UDynamicMesh* TargetMesh, int32 VertexID );

	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries")
	static UPARAM(DisplayName = "Position") FVector GetVertexPosition( UDynamicMesh* TargetMesh, int32 VertexID, bool& bIsValidVertex );

	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries")
	static UPARAM(DisplayName = "Positions") TArray<FVector> GetAllVertexPositions( UDynamicMesh* TargetMesh, bool& bHasVertexIDGaps );



	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries")
	static UPARAM(DisplayName = "Num UV Sets") int32 GetNumUVSets( UDynamicMesh* TargetMesh );

	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries")
	static UPARAM(DisplayName = "Bounding Box") FBox2D GetUVSetBoundingBox( UDynamicMesh* TargetMesh, int UVSetIndex, bool& bIsValidUVSet, bool& bUVSetIsEmpty );

	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries")
	static void GetTriangleUVs( UDynamicMesh* TargetMesh, int32 UVSetIndex, int32 TriangleID, FVector2D& UV1, FVector2D& UV2, FVector2D& UV3, bool& bHaveValidUVs );


};