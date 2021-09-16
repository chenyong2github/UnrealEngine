// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "MeshUVFunctions.generated.h"

class UDynamicMesh;


USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptRepackUVsOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	int TargetImageWidth = 512;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bOptimizeIslandRotation = true;
};


UCLASS(meta = (ScriptName = "GeometryScript_UVs"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_MeshUVFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|UVs", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	SetNumUVSets( 
		UDynamicMesh* TargetMesh, 
		int NumUVSets,
		UGeometryScriptDebug* Debug = nullptr );


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|UVs", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	SetMeshTriangleUVs( 
		UDynamicMesh* TargetMesh, 
		int UVSetIndex,
		int TriangleID, 
		FGeometryScriptUVTriangle UVs,
		bool& bIsValidTriangle, 
		bool bDeferChangeNotifications = false );


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|UVs", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	TranslateMeshUVs( 
		UDynamicMesh* TargetMesh, 
		int UVSetIndex,
		FVector2D Translation,
		UGeometryScriptDebug* Debug = nullptr );

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|UVs", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ScaleMeshUVs( 
		UDynamicMesh* TargetMesh, 
		int UVSetIndex,
		FVector2D Scale,
		FVector2D ScaleOrigin,
		UGeometryScriptDebug* Debug = nullptr );

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|UVs", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	RotateMeshUVs( 
		UDynamicMesh* TargetMesh, 
		int UVSetIndex,
		float RotationAngle,
		FVector2D RotationOrigin,
		UGeometryScriptDebug* Debug = nullptr );


	/**
	 * Scale of PlaneTransform defines world-space dimension that maps to 1 UV dimension
	 */
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|UVs", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	SetMeshUVsFromPlanarProjection( 
		UDynamicMesh* TargetMesh, 
		int UVSetIndex,
		FTransform PlaneTransform,
		UGeometryScriptDebug* Debug = nullptr );


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|UVs", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	SetMeshUVsFromBoxProjection( 
		UDynamicMesh* TargetMesh, 
		int UVSetIndex,
		FTransform BoxTransform,
		int MinIslandTriCount = 2,
		UGeometryScriptDebug* Debug = nullptr );


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|UVs", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	RepackMeshUVs( 
		UDynamicMesh* TargetMesh, 
		int UVSetIndex,
		FGeometryScriptRepackUVsOptions RepackOptions,
		UGeometryScriptDebug* Debug = nullptr );

};