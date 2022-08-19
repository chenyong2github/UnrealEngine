// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "MeshModelingFunctions.generated.h"

class UDynamicMesh;

UENUM(BlueprintType)
enum class EGeometryScriptMeshEditPolygroupMode : uint8
{
	PreserveExisting = 0,
	AutoGenerateNew = 1,
	SetConstant = 2
};

USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptMeshEditPolygroupOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	EGeometryScriptMeshEditPolygroupMode GroupMode = EGeometryScriptMeshEditPolygroupMode::PreserveExisting;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	int ConstantGroup = 0;
};


USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptMeshOffsetOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	float OffsetDistance = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	bool bFixedBoundary = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	int SolveSteps = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	float SmoothAlpha = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	bool bReprojectDuringSmoothing = false;

	// should not be > 0.9
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	float BoundaryAlpha = 0.2f;
};



USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptMeshExtrudeOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	float ExtrudeDistance = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	FVector ExtrudeDirection = FVector(0,0,1);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	float UVScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	bool bSolidsToShells = true;
};


UENUM(BlueprintType)
enum class EGeometryScriptLinearExtrudeDirection : uint8
{
	FixedDirection = 0,
	AverageFaceNormal = 1
};

UENUM(BlueprintType)
enum class EGeometryScriptLinearExtrudeArea : uint8
{
	EntireSelection = 0,
	PerPolygroup = 1,
	PerTriangle = 2
};

USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptMeshLinearExtrudeOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	float Distance = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	EGeometryScriptLinearExtrudeDirection DirectionMode = EGeometryScriptLinearExtrudeDirection::FixedDirection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	FVector Direction = FVector(0,0,1);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	EGeometryScriptLinearExtrudeArea AreaMode = EGeometryScriptLinearExtrudeArea::EntireSelection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	FGeometryScriptMeshEditPolygroupOptions GroupOptions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	float UVScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	bool bSolidsToShells = true;
};




USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptMeshBevelOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	float BevelDistance = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	bool bInferMaterialID = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
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


/**
 * Mode passed to ApplyMeshBevelSelection to control how the input Selection should
 * be interpreted as selecting an area of the mesh to Bevel
 */
UENUM(BlueprintType)
enum class EGeometryScriptMeshBevelSelectionMode : uint8
{
	/** Convert the selection to Triangles and bevel the boundary edge loops of the triangle set */
	TriangleArea = 0,
	/** Convert the selection to Polygroups and bevel all the Polygroup Edges of the selected Polygroups */
	AllPolygroupEdges = 1,
	/** Convert the selection to Polygroups and bevel all the Polygroup Edges that are between selected Polygroups */
	SharedPolygroupEdges = 2
};


USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptMeshBevelSelectionOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	float BevelDistance = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	bool bInferMaterialID = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	int SetMaterialID = 0;
};



UCLASS(meta = (ScriptName = "GeometryScript_MeshModeling"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_MeshModelingFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	/**
	 * Disconnect the triangles of TargetMesh identified by the Selection.
	 * The input Selection will still identify the same geometric elements after Disconnecting.
	 * @param bAllowBowtiesInOutput if false, any bowtie vertices resulting created in the Duplicate area will be disconnected into unique vertices
	 */
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Modeling", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyMeshDisconnectFaces(
		UDynamicMesh* TargetMesh,
		FGeometryScriptMeshSelection Selection,
		bool bAllowBowtiesInOutput = true,
		UGeometryScriptDebug* Debug = nullptr );

	/**
	 * Duplicate the triangles of TargetMesh identified by the Selection
	 * @param NewTriangles a Mesh Selection of the duplicate triangles is returned here (with type Triangles)
	 * @param bAllowBowtiesInOutput if false, any bowtie vertices resulting created in the Duplicate area will be disconnected into unique vertices
	 */
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Modeling", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyMeshDuplicateFaces(
		UDynamicMesh* TargetMesh,
		FGeometryScriptMeshSelection Selection,
		FGeometryScriptMeshSelection& NewTriangles,
		FGeometryScriptMeshEditPolygroupOptions GroupOptions = FGeometryScriptMeshEditPolygroupOptions(),
		UGeometryScriptDebug* Debug = nullptr );




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


	/**
	 * Apply Linear Extrusion (ie extrusion in a single direction) to the triangles of TargetMesh identified by the Selection.
	 * The input Selection will still identify the same geometric elements after the Extrusion
	 */
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Modeling", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyMeshLinearExtrudeFaces(
		UDynamicMesh* TargetMesh,
		FGeometryScriptMeshLinearExtrudeOptions Options,
		FGeometryScriptMeshSelection Selection,
		UGeometryScriptDebug* Debug = nullptr );



	/**
	 * Apply a Mesh Bevel operation to parts of TargetMesh using the BevelOptions settings.
	 * @param Selection specifies which mesh edges to Bevel
	 * @param BevelMode specifies how Selection should be converted to a Triangle Region or set of Polygroup Edges
	 * @param BevelOptions settings for the Bevel Operation
	 */
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Modeling", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyMeshBevelSelection(
		UDynamicMesh* TargetMesh,
		FGeometryScriptMeshSelection Selection,
		EGeometryScriptMeshBevelSelectionMode BevelMode,
		FGeometryScriptMeshBevelSelectionOptions BevelOptions,
		UGeometryScriptDebug* Debug = nullptr );


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Modeling", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyMeshPolygroupBevel(
		UDynamicMesh* TargetMesh,
		FGeometryScriptMeshBevelOptions Options,
		UGeometryScriptDebug* Debug = nullptr );




	//---------------------------------------------
	// Backwards-Compatibility implementations
	//---------------------------------------------
	// These are versions/variants of the above functions that were released
	// in previous UE 5.x versions, that have since been updated. 
	// To avoid breaking user scripts, these previous versions are currently kept and 
	// called via redirectors registered in GeometryScriptingCoreModule.cpp.
	// 
	// These functions may be deprecated in future UE releases.
	//


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Compatibility", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyMeshExtrude_Compatibility_5p0(
		UDynamicMesh* TargetMesh,
		FGeometryScriptMeshExtrudeOptions Options,
		UGeometryScriptDebug* Debug = nullptr );

};