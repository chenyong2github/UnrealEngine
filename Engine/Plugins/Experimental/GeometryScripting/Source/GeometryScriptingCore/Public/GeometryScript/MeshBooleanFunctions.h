// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "MeshBooleanFunctions.generated.h"



UENUM(BlueprintType)
enum class EGeometryScriptBooleanOperation : uint8
{
	Union,
	Intersection,
	Subtract
};



USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptMeshBooleanOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bFillHoles = true;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bSimplifyOutput = true;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	float SimplifyPlanarTolerance = 0.01f;
};


USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptMeshSelfUnionOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bFillHoles = true;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bTrimFlaps = true;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bSimplifyOutput = true;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	float SimplifyPlanarTolerance = 0.01f;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	float WindingThreshold = 0.5f;
};



USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptMeshPlaneCutOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bFillHoles = true;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bFillSpans = true;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bFlipCutSide = false;
};


USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptMeshMirrorOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bApplyPlaneCut = true;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bFlipCutSide = false;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bWeldAlongPlane = true;
};



UCLASS(meta = (ScriptName = "GeometryScript_MeshBooleans"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_MeshBooleanFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Booleans", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyMeshBoolean( 
		UDynamicMesh* TargetMesh, 
		FTransform TargetTransform,
		UDynamicMesh* ToolMesh, 
		FTransform ToolTransform,
		EGeometryScriptBooleanOperation Operation,
		FGeometryScriptMeshBooleanOptions Options,
		UGeometryScriptDebug* Debug = nullptr);


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Booleans", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyMeshSelfUnion( 
		UDynamicMesh* TargetMesh, 
		FGeometryScriptMeshSelfUnionOptions Options,
		UGeometryScriptDebug* Debug = nullptr);



	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Booleans", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyMeshPlaneCut( 
		UDynamicMesh* TargetMesh, 
		FVector CutPlaneOrigin,
		FVector CutPlaneNormal,
		FGeometryScriptMeshPlaneCutOptions Options,
		UGeometryScriptDebug* Debug = nullptr);


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Booleans", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyMeshMirror( 
		UDynamicMesh* TargetMesh, 
		FVector MirrorPlaneOrigin,
		FVector MirrorPlaneNormal,
		FGeometryScriptMeshMirrorOptions Options,
		UGeometryScriptDebug* Debug = nullptr);

};
