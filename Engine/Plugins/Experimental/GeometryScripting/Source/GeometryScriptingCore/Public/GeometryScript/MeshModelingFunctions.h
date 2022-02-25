// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "MeshModelingFunctions.generated.h"

class UDynamicMesh;


USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptMeshOffsetOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	float OffsetDistance = 1.0;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bFixedBoundary = false;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	int SolveSteps = 5;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	float SmoothAlpha = 0.1f;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bReprojectDuringSmoothing = false;

	// should not be > 0.9
	UPROPERTY(BlueprintReadWrite, Category = Options)
	float BoundaryAlpha = 0.2f;
};



USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptMeshExtrudeOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	float ExtrudeDistance = 1.0;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	FVector ExtrudeDirection = FVector(0,0,1);

	UPROPERTY(BlueprintReadWrite, Category = Options)
	float UVScale = 1.0f;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bSolidsToShells = true;
};



USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptMeshBevelOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	float BevelDistance = 1.0;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bInferMaterialID = false;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	int SetMaterialID = 0;



	/**
	 * If true the set of beveled polygroup edges is limited to those that 
	 * are fully or partially contained within the (transformed) FilterBox
	 */
	UPROPERTY(BlueprintReadWrite, Category = FilterShape, AdvancedDisplay)
	bool bApplyFilterBox = false;

	/**
	 * Bounding Box used for edge filtering
	 */
	UPROPERTY(BlueprintReadWrite, Category = FilterShape, AdvancedDisplay)
	FBox FilterBox = FBox(EForceInit::ForceInit);

	/**
	 * Transform applied to the FilterBox
	 */
	UPROPERTY(BlueprintReadWrite, Category = FilterShape, AdvancedDisplay)
	FTransform FilterBoxTransform = FTransform::Identity;

	/**
	 * If true, then only polygroup edges that are fully contained within the filter box will be beveled,
	 * otherwise the edge will be beveled if any vertex is within the filter box.
	 */
	UPROPERTY(BlueprintReadWrite, Category = FilterShape, AdvancedDisplay)
	bool bFullyContained = true;
};




UCLASS(meta = (ScriptName = "GeometryScript_MeshModeling"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_MeshModelingFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Modeling", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyMeshOffset(
		UDynamicMesh* TargetMesh,
		FGeometryScriptMeshOffsetOptions Options,
		UGeometryScriptDebug* Debug = nullptr );

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Modeling", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyMeshShell(
		UDynamicMesh* TargetMesh,
		FGeometryScriptMeshOffsetOptions Options,
		UGeometryScriptDebug* Debug = nullptr );

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Modeling", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyMeshExtrude(
		UDynamicMesh* TargetMesh,
		FGeometryScriptMeshExtrudeOptions Options,
		UGeometryScriptDebug* Debug = nullptr );

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Modeling", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyMeshPolygroupBevel(
		UDynamicMesh* TargetMesh,
		FGeometryScriptMeshBevelOptions Options,
		UGeometryScriptDebug* Debug = nullptr );

};