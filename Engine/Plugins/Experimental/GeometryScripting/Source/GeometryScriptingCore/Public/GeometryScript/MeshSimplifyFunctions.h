// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "MeshSimplifyFunctions.generated.h"

class UDynamicMesh;

USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptPlanarSimplifyOptions
{
	GENERATED_BODY()
public:
	/** */
	UPROPERTY(BlueprintReadWrite, Category = Options)
	float AngleThreshold = 0.001;
};

USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptPolygroupSimplifyOptions
{
	GENERATED_BODY()
public:
	/** */
	UPROPERTY(BlueprintReadWrite, Category = Options)
	float AngleThreshold = 0.001;
};


UENUM(BlueprintType)
enum class EGeometryScriptRemoveMeshSimplificationType : uint8
{
	StandardQEM = 0,
	VolumePreserving = 1,
	AttributeAware = 2
};


USTRUCT(Blueprintable)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptSimplifyMeshOptions
{
	GENERATED_BODY()
public:

	UPROPERTY(BlueprintReadWrite, Category = Options)
	EGeometryScriptRemoveMeshSimplificationType Method = EGeometryScriptRemoveMeshSimplificationType::AttributeAware;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bAllowSeamCollapse = true;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bAllowSeamSmoothing = true;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bAllowSeamSplits = true;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bPreserveVertexPositions = false;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bRetainQuadricMemory = false;
};




UCLASS(meta = (ScriptName = "GeometryScript_MeshSimplification"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_MeshSimplifyFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Simplification", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplySimplifyToPlanar(  
		UDynamicMesh* TargetMesh, 
		FGeometryScriptPlanarSimplifyOptions Options,
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Simplification", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplySimplifyToPolygroupTopology(  
		UDynamicMesh* TargetMesh, 
		FGeometryScriptPolygroupSimplifyOptions Options,
		FGeometryScriptGroupLayer GroupLayer,
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Simplification", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplySimplifyToTriangleCount(  
		UDynamicMesh* TargetMesh, 
		int32 TriangleCount,
		FGeometryScriptSimplifyMeshOptions Options,
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Simplification", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplySimplifyToVertexCount(  
		UDynamicMesh* TargetMesh, 
		int32 VertexCount,
		FGeometryScriptSimplifyMeshOptions Options,
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Simplification", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplySimplifyToTolerance(  
		UDynamicMesh* TargetMesh, 
		float Tolerance,
		FGeometryScriptSimplifyMeshOptions Options,
		UGeometryScriptDebug* Debug = nullptr);

};