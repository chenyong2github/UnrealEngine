// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "MeshAssetFunctions.generated.h"

class UStaticMesh;
class UDynamicMesh;
class UMaterialInterface;



USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptCopyMeshFromAssetOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bApplyBuildSettings = true;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bRequestTangents = true;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bIgnoreRemoveDegenerates = true;
};



USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptCopyMeshToAssetOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bEnableRecomputeNormals = false;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bEnableRecomputeTangents = false;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bEnableRemoveDegenerates = false;

	
	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bReplaceMaterials = false;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	TArray<UMaterialInterface*> NewMaterials;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	TArray<FName> NewMaterialSlotNames;


	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bEmitTransaction = true;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bDeferMeshPostEditChange = false;
};



// Although the class name indicates StaticMeshFunctions, that was a naming mistake that is difficult
// to correct. This class is intended to serve as a generic asset utils function library. The naming
// issue is only visible at the C++ level. It is not visible in Python or BP.
UCLASS(meta = (ScriptName = "GeometryScript_AssetUtils"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_StaticMeshFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|StaticMesh", meta = (ExpandEnumAsExecs = "Outcome"))
	static UPARAM(DisplayName = "Dynamic Mesh") UDynamicMesh* 
	CopyMeshFromStaticMesh(
		UStaticMesh* FromStaticMeshAsset, 
		UDynamicMesh* ToDynamicMesh, 
		FGeometryScriptCopyMeshFromAssetOptions AssetOptions,
		FGeometryScriptMeshReadLOD RequestedLOD,
		TEnumAsByte<EGeometryScriptOutcomePins>& Outcome,
		UGeometryScriptDebug* Debug = nullptr);


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|StaticMesh", meta = (ExpandEnumAsExecs = "Outcome"))
	static UPARAM(DisplayName = "Dynamic Mesh") UDynamicMesh* 
	CopyMeshToStaticMesh(
		UDynamicMesh* FromDynamicMesh, 
		UStaticMesh* ToStaticMeshAsset, 
		FGeometryScriptCopyMeshToAssetOptions Options,
		FGeometryScriptMeshWriteLOD TargetLOD,
		TEnumAsByte<EGeometryScriptOutcomePins>& Outcome,
		UGeometryScriptDebug* Debug = nullptr);



	UFUNCTION(BlueprintCallable, Category = "GeometryScript|StaticMesh", meta = (ExpandEnumAsExecs = "Outcome"))
	static void
	GetSectionMaterialListFromStaticMesh(
		UStaticMesh* FromStaticMeshAsset, 
		FGeometryScriptMeshReadLOD RequestedLOD,
		TArray<UMaterialInterface*>& MaterialList,
		TArray<int32>& MaterialIndex,
		TEnumAsByte<EGeometryScriptOutcomePins>& Outcome,
		UGeometryScriptDebug* Debug = nullptr);


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|SkeletalMesh", meta = (ExpandEnumAsExecs = "Outcome"))
	static UPARAM(DisplayName = "Dynamic Mesh") UDynamicMesh* 
	CopyMeshFromSkeletalMesh(
		USkeletalMesh* FromSkeletalMeshAsset, 
		UDynamicMesh* ToDynamicMesh,
		FGeometryScriptCopyMeshFromAssetOptions AssetOptions,
		FGeometryScriptMeshReadLOD RequestedLOD,
		TEnumAsByte<EGeometryScriptOutcomePins>& Outcome,
		UGeometryScriptDebug* Debug = nullptr);


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|SkeletalMesh", meta = (ExpandEnumAsExecs = "Outcome"))
	static UPARAM(DisplayName = "Dynamic Mesh") UDynamicMesh* 
	CopyMeshToSkeletalMesh(
		UDynamicMesh* FromDynamicMesh, 
		USkeletalMesh* ToSkeletalMeshAsset,
		FGeometryScriptCopyMeshToAssetOptions Options,
		FGeometryScriptMeshWriteLOD TargetLOD,
		TEnumAsByte<EGeometryScriptOutcomePins>& Outcome,
		UGeometryScriptDebug* Debug = nullptr);
};


