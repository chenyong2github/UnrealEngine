// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "ContainmentFunctions.generated.h"

class UDynamicMesh;

USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptConvexHullOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bPrefilterVertices = true;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	int PrefilterGridResolution = 128;

	/** */
	UPROPERTY(BlueprintReadWrite, Category = Options)
	int SimplifyToFaceCount = 0;
};



USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptSweptHullOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bPrefilterVertices = true;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	int PrefilterGridResolution = 128;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	float MinThickness = 0.01;

	/** */
	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bSimplify = true;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	float MinEdgeLength = 0.1;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	float SimplifyTolerance = 0.1;
};





UCLASS(meta = (ScriptName = "GeometryScript_Containment"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_ContainmentFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Containment", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ComputeMeshConvexHull(  
		UDynamicMesh* TargetMesh, 
		UPARAM(DisplayName = "Hull Mesh", ref) UDynamicMesh* CopyToMesh, 
		UPARAM(DisplayName = "Hull Mesh") UDynamicMesh*& CopyToMeshOut, 
		FGeometryScriptConvexHullOptions Options,
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Containment", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ComputeMeshSweptHull(  
		UDynamicMesh* TargetMesh, 
		UPARAM(DisplayName = "Hull Mesh", ref) UDynamicMesh* CopyToMesh, 
		UPARAM(DisplayName = "Hull Mesh") UDynamicMesh*& CopyToMeshOut, 
		FTransform ProjectionFrame,
		FGeometryScriptSweptHullOptions Options,
		UGeometryScriptDebug* Debug = nullptr);

};