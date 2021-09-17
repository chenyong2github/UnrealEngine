// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "MeshNormalsFunctions.generated.h"

class UDynamicMesh;

USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptCalculateNormalsOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bAngleWeighted = true;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bAreaWeighted = true;
};



USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptSplitNormalsOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bSplitByOpeningAngle = true;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	float OpeningAngleDeg = 15.0f;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bSplitByFaceGroup = false;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	FGeometryScriptGroupLayer GroupLayer;
};


UENUM(BlueprintType)
enum class EGeometryScriptTangentTypes : uint8
{
	FastMikkT = 0,
	PerTriangle = 1
};


USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptTangentsOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	EGeometryScriptTangentTypes Type = EGeometryScriptTangentTypes::FastMikkT;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	int UVLayer = 0;
};



UCLASS(meta = (ScriptName = "GeometryScript_Normals"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_MeshNormalsFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Normals", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	FlipNormals( 
		UDynamicMesh* TargetMesh, 
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Normals", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AutoRepairNormals( 
		UDynamicMesh* TargetMesh, 
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Normals", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	SetPerVertexNormals( 
		UDynamicMesh* TargetMesh, 
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Normals", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	SetPerFaceNormals( 
		UDynamicMesh* TargetMesh, 
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Normals", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	RecomputeNormals(  
		UDynamicMesh* TargetMesh, 
		FGeometryScriptCalculateNormalsOptions CalculateOptions,
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Normals", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ComputeSplitNormals( 
		UDynamicMesh* TargetMesh, 
		FGeometryScriptSplitNormalsOptions SplitOptions,
		FGeometryScriptCalculateNormalsOptions CalculateOptions,
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Normals", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ComputeTangents( 
		UDynamicMesh* TargetMesh, 
		FGeometryScriptTangentsOptions Options,
		UGeometryScriptDebug* Debug = nullptr);


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Normals", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	SetMeshTriangleNormals( 
		UDynamicMesh* TargetMesh, 
		int TriangleID, 
		FGeometryScriptTriangle Normals,
		bool& bIsValidTriangle, 
		bool bDeferChangeNotifications = false );

};