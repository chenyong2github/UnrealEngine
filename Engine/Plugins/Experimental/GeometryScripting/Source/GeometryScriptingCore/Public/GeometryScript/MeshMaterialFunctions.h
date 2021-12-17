// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "MeshMaterialFunctions.generated.h"

class UDynamicMesh;


UCLASS(meta = (ScriptName = "GeometryScript_Materials"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_MeshMaterialFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintPure, Category = "GeometryScript|MeshQueries", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Enabled") int GetMaxMaterialID( UDynamicMesh* TargetMesh, bool& bHasMaterialIDs );

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Materials", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	EnableMaterialIDs( UDynamicMesh* TargetMesh, UGeometryScriptDebug* Debug = nullptr );

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Materials", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ClearMaterialIDs( UDynamicMesh* TargetMesh, int ClearValue = 0, UGeometryScriptDebug* Debug = nullptr );

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Materials", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	RemapMaterialIDs( 
		UDynamicMesh* TargetMesh, 
		int FromMaterialID,
		int ToMaterialID,
		UGeometryScriptDebug* Debug = nullptr );

	UFUNCTION(BlueprintPure, Category = "GeometryScript|Materials", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Material ID") int32
	GetTriangleMaterialID( UDynamicMesh* TargetMesh, int TriangleID, bool& bIsValidTriangle );

	UFUNCTION(BlueprintPure, Category = "GeometryScript|Materials", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	GetAllTriangleMaterialIDs( UDynamicMesh* TargetMesh, FGeometryScriptIndexList& MaterialIDList, bool& bHasMaterialIDs );


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Materials", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	SetTriangleMaterialID( 
		UDynamicMesh* TargetMesh, 
		int TriangleID, 
		int MaterialID,
		bool& bIsValidTriangle, 
		bool bDeferChangeNotifications = false );

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Materials", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh*
	SetAllTriangleMaterialIDs(
		UDynamicMesh* TargetMesh,
		FGeometryScriptIndexList& TriangleMaterialIDList,
		bool bDeferChangeNotifications = false,
		UGeometryScriptDebug* Debug = nullptr);


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Materials", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	SetPolygroupMaterialID( 
		UDynamicMesh* TargetMesh, 
		FGeometryScriptGroupLayer GroupLayer,
		int PolygroupID, 
		int MaterialID,
		bool& bIsValidPolygroupID,
		bool bDeferChangeNotifications = false,
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Materials", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	DeleteTrianglesByMaterialID( 
		UDynamicMesh* TargetMesh, 
		int MaterialID,
		int& NumDeleted,
		bool bDeferChangeNotifications = false,
		UGeometryScriptDebug* Debug = nullptr);

	/**
	 * Compact the MaterialIDs of the TargetMesh, ie remove any un-used MaterialIDs and remap the remaining
	 * N in-use MaterialIDs to the range [0,N-1]. Optionally compute a Compacted list of Materials.
	 * @param SourceMaterialList Input Material list, assumption is that SourceMaterialList.Num() == number of MaterialIDs on mesh at input
	 * @param CompactedMaterialList new Compacted Material list, one-to-one with new compacted MaterialIDs
	 */
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Materials", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	CompactMaterialIDs( 
		UDynamicMesh* TargetMesh, 
		TArray<UMaterialInterface*> SourceMaterialList,
		TArray<UMaterialInterface*>& CompactedMaterialList,
		UGeometryScriptDebug* Debug = nullptr);

};