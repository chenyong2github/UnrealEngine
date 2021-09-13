// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "MeshBasicEditFunctions.generated.h"

class UDynamicMesh;



UCLASS(meta = (ScriptName = "GeometryScript_MeshEdits"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_MeshBasicEditFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|MeshEdits", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	DiscardMeshAttributes( 
		UDynamicMesh* TargetMesh, 
		bool bDeferChangeNotifications = false );



	UFUNCTION(BlueprintCallable, Category = "GeometryScript|MeshEdits", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	SetVertexPosition( 
		UDynamicMesh* TargetMesh, 
		int VertexID, 
		FVector NewPosition, 
		bool& bIsValidVertex, 
		bool bDeferChangeNotifications = false );


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|MeshEdits", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AddVertexToMesh( 
		UDynamicMesh* TargetMesh, 
		FVector NewPosition, 
		int& NewVertexIndex,
		bool bDeferChangeNotifications = false );

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|MeshEdits", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AddVerticesToMesh( 
		UDynamicMesh* TargetMesh, 
		const TArray<FVector>& NewPositions, 
		TArray<int>& NewIndices,
		bool bDeferChangeNotifications = false );

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|MeshEdits", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	DeleteVertexFromMesh( 
		UDynamicMesh* TargetMesh, 
		int VertexID,
		bool& bWasVertexDeleted,
		bool bDeferChangeNotifications = false );



	UFUNCTION(BlueprintCallable, Category = "GeometryScript|MeshEdits", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AddTriangleToMesh( 
		UDynamicMesh* TargetMesh, 
		FIntVector NewTriangle,
		int& NewTriangleIndex,
		int NewTriangleGroupID = 0,
		bool bDeferChangeNotifications = false,
		UGeometryScriptDebug* Debug = nullptr );


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|MeshEdits", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AddTrianglesToMesh( 
		UDynamicMesh* TargetMesh, 
		const TArray<FIntVector>& NewTriangles,
		TArray<int>& NewIndices,
		int NewTriangleGroupID = 0,
		bool bDeferChangeNotifications = false,
		UGeometryScriptDebug* Debug = nullptr );

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|MeshEdits", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	DeleteTriangleFromMesh( 
		UDynamicMesh* TargetMesh, 
		int TriangleID,
		bool& bWasTriangleDeleted,
		bool bDeferChangeNotifications = false );




	UFUNCTION(BlueprintCallable, Category = "GeometryScript|MeshEdits", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AppendMesh( 
		UDynamicMesh* TargetMesh, 
		UDynamicMesh* AppendMesh, 
		FTransform AppendTransform, 
		bool bDeferChangeNotifications = false,
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|MeshEdits", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AppendMeshRepeated( 
		UDynamicMesh* TargetMesh, 
		UDynamicMesh* AppendMesh, 
		FTransform AppendTransform, 
		int RepeatCount = 1,
		bool bApplyTransformToFirstInstance = true,
		bool bDeferChangeNotifications = false,
		UGeometryScriptDebug* Debug = nullptr);


};