// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "MeshDecompositionFunctions.generated.h"

class UDynamicMesh;


UCLASS(meta = (ScriptName = "GeometryScript_MeshDecomposition"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_MeshDecompositionFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Decomposition", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	SplitMeshByComponents(  
		UDynamicMesh* TargetMesh, 
		TArray<UDynamicMesh*>& ComponentMeshes,
		UDynamicMeshPool* MeshPool,
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Decomposition", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	SplitMeshByMaterialIDs(  
		UDynamicMesh* TargetMesh, 
		TArray<UDynamicMesh*>& ComponentMeshes,
		TArray<int>& ComponentMaterialIDs,
		UDynamicMeshPool* MeshPool,
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Decomposition", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	SplitMeshByPolygroups(  
		UDynamicMesh* TargetMesh, 
		FGeometryScriptGroupLayer GroupLayer,
		TArray<UDynamicMesh*>& ComponentMeshes,
		TArray<int>& ComponentPolygroups,
		UDynamicMeshPool* MeshPool,
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Decomposition", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	GetSubMeshFromMesh(  
		UDynamicMesh* TargetMesh, 
		UPARAM(DisplayName = "Copy To Submesh", ref) UDynamicMesh* StoreToSubmesh, 
		FGeometryScriptIndexList TriangleList,
		UPARAM(DisplayName = "Copy To Submesh") UDynamicMesh*& StoreToSubmeshOut, 
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Decomposition", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Copy From Mesh") UDynamicMesh* 
	CopyMeshToMesh(  
		UDynamicMesh* CopyFromMesh, 
		UPARAM(DisplayName = "Copy To Mesh", ref) UDynamicMesh* CopyToMesh, 
		UPARAM(DisplayName = "Copy To Mesh") UDynamicMesh*& CopyToMeshOut, 
		UGeometryScriptDebug* Debug = nullptr);

};