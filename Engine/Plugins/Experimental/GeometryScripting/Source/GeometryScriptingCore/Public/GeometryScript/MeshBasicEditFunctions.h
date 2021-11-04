// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "MeshBasicEditFunctions.generated.h"

class UDynamicMesh;


USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptSimpleMeshBuffers
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	TArray<FVector> Vertices;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	TArray<FVector> Normals;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	TArray<FVector2D> UV0;
	UPROPERTY(BlueprintReadWrite, Category = Options)
	TArray<FVector2D> UV1;
	UPROPERTY(BlueprintReadWrite, Category = Options)
	TArray<FVector2D> UV2;
	UPROPERTY(BlueprintReadWrite, Category = Options)
	TArray<FVector2D> UV3;
	UPROPERTY(BlueprintReadWrite, Category = Options)
	TArray<FVector2D> UV4;
	UPROPERTY(BlueprintReadWrite, Category = Options)
	TArray<FVector2D> UV5;
	UPROPERTY(BlueprintReadWrite, Category = Options)
	TArray<FVector2D> UV6;
	UPROPERTY(BlueprintReadWrite, Category = Options)
	TArray<FVector2D> UV7;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	TArray<FLinearColor> VertexColors;


	UPROPERTY(BlueprintReadWrite, Category = Options)
	TArray<FIntVector> Triangles;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	TArray<int> TriGroupIDs;
};


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
		FGeometryScriptVectorList NewPositionsList, 
		FGeometryScriptIndexList& NewIndicesList,
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
	DeleteVerticesFromMesh( 
		UDynamicMesh* TargetMesh, 
		FGeometryScriptIndexList VertexList,
		int& NumDeleted,
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
		FGeometryScriptTriangleList NewTrianglesList,
		FGeometryScriptIndexList& NewIndicesList,
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
	DeleteTrianglesFromMesh( 
		UDynamicMesh* TargetMesh, 
		FGeometryScriptIndexList TriangleList,
		int& NumDeleted,
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



	UFUNCTION(BlueprintCallable, Category = "GeometryScript|MeshEdits", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AppendBuffersToMesh( 
		UDynamicMesh* TargetMesh, 
		const FGeometryScriptSimpleMeshBuffers& Buffers,
		FGeometryScriptIndexList& NewTriangleIndicesList,
		int MaterialID = 0,
		bool bDeferChangeNotifications = false,
		UGeometryScriptDebug* Debug = nullptr );

};