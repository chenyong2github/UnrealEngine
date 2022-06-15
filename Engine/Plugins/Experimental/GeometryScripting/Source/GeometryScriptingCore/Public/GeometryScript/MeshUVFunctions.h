// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "MeshUVFunctions.generated.h"

class UDynamicMesh;


USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptRepackUVsOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	int TargetImageWidth = 512;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	bool bOptimizeIslandRotation = true;
};

UENUM(BlueprintType)
enum class EGeometryScriptUVFlattenMethod : uint8
{
	ExpMap = 0,
	Conformal = 1,
	SpectralConformal = 2
};

UENUM(BlueprintType)
enum class EGeometryScriptUVIslandSource : uint8
{
	PolyGroups = 0,
	UVIslands = 1
};


USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptExpMapUVOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	int NormalSmoothingRounds = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	float NormalSmoothingAlpha = 0.25f;
};

USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptSpectralConformalUVOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	bool bPreserveIrregularity = true;
};


USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptRecomputeUVsOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	EGeometryScriptUVFlattenMethod Method = EGeometryScriptUVFlattenMethod::SpectralConformal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	EGeometryScriptUVIslandSource IslandSource = EGeometryScriptUVIslandSource::UVIslands;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	FGeometryScriptExpMapUVOptions ExpMapOptions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	FGeometryScriptSpectralConformalUVOptions SpectralConformalOptions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	FGeometryScriptGroupLayer GroupLayer;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	bool bAutoAlignIslandsWithAxes = true;
};





USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptPatchBuilderOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	int InitialPatchCount = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	int MinPatchSize = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	float PatchCurvatureAlignmentWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	float PatchMergingMetricThresh = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	float PatchMergingAngleThresh = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	FGeometryScriptExpMapUVOptions ExpMapOptions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	bool bRespectInputGroups = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	FGeometryScriptGroupLayer GroupLayer;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	bool bAutoPack = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	FGeometryScriptRepackUVsOptions PackingOptions;
};




USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptXAtlasOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	int MaxIterations = 2;
};



UCLASS(meta = (ScriptName = "GeometryScript_UVs"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_MeshUVFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|UVs", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	SetNumUVSets( 
		UDynamicMesh* TargetMesh, 
		int NumUVSets,
		UGeometryScriptDebug* Debug = nullptr );

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|UVs", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	CopyUVSet( 
		UDynamicMesh* TargetMesh, 
		int FromUVSet,
		int ToUVSet,
		UGeometryScriptDebug* Debug = nullptr );


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|UVs", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	SetMeshTriangleUVs( 
		UDynamicMesh* TargetMesh, 
		int UVSetIndex,
		int TriangleID, 
		FGeometryScriptUVTriangle UVs,
		bool& bIsValidTriangle, 
		bool bDeferChangeNotifications = false );


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|UVs", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	TranslateMeshUVs( 
		UDynamicMesh* TargetMesh, 
		int UVSetIndex,
		FVector2D Translation,
		UGeometryScriptDebug* Debug = nullptr );

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|UVs", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ScaleMeshUVs( 
		UDynamicMesh* TargetMesh, 
		int UVSetIndex,
		FVector2D Scale,
		FVector2D ScaleOrigin,
		UGeometryScriptDebug* Debug = nullptr );

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|UVs", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	RotateMeshUVs( 
		UDynamicMesh* TargetMesh, 
		int UVSetIndex,
		float RotationAngle,
		FVector2D RotationOrigin,
		UGeometryScriptDebug* Debug = nullptr );


	/**
	 * Scale of PlaneTransform defines world-space dimension that maps to 1 UV dimension
	 */
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|UVs", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	SetMeshUVsFromPlanarProjection( 
		UDynamicMesh* TargetMesh, 
		int UVSetIndex,
		FTransform PlaneTransform,
		UGeometryScriptDebug* Debug = nullptr );


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|UVs", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	SetMeshUVsFromBoxProjection( 
		UDynamicMesh* TargetMesh, 
		int UVSetIndex,
		FTransform BoxTransform,
		int MinIslandTriCount = 2,
		UGeometryScriptDebug* Debug = nullptr );


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|UVs", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	SetMeshUVsFromCylinderProjection( 
		UDynamicMesh* TargetMesh, 
		int UVSetIndex,
		FTransform CylinderTransform,
		float SplitAngle = 45.0,
		UGeometryScriptDebug* Debug = nullptr );



	UFUNCTION(BlueprintCallable, Category = "GeometryScript|UVs", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	RecomputeMeshUVs( 
		UDynamicMesh* TargetMesh, 
		int UVSetIndex,
		FGeometryScriptRecomputeUVsOptions Options,
		UGeometryScriptDebug* Debug = nullptr );

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|UVs", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	RepackMeshUVs( 
		UDynamicMesh* TargetMesh, 
		int UVSetIndex,
		FGeometryScriptRepackUVsOptions RepackOptions,
		UGeometryScriptDebug* Debug = nullptr );

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|UVs", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AutoGeneratePatchBuilderMeshUVs( 
		UDynamicMesh* TargetMesh, 
		int UVSetIndex,
		FGeometryScriptPatchBuilderOptions Options,
		UGeometryScriptDebug* Debug = nullptr );

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|UVs", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AutoGenerateXAtlasMeshUVs( 
		UDynamicMesh* TargetMesh, 
		int UVSetIndex,
		FGeometryScriptXAtlasOptions Options,
		UGeometryScriptDebug* Debug = nullptr );


	/**
	 * Get a list of single vertex UVs for each mesh vertex in the TargetMesh, derived from the specified UV Overlay.
	 * The UV Overlay may store multiple UVs for a single vertex (along UV seams)
	 * In such cases an arbitrary UV will be stored for that vertex, and bHasSplitUVs will be returned as true
	 * @param UVSetIndex index of UV Set to read
	 * @param UVList output UV list will be stored here. Size will be equal to the MaxVertexID of TargetMesh  (not the VertexCount!)
	 * @param bIsValidUVSet will be set to true if the UV Overlay was valid
	 * @param bHasVertexIDGaps will be set to true if some vertex indices in TargetMesh were invalid, ie MaxVertexID > VertexCount 
	 * @param bHasSplitUVs will be set to true if there were split UVs in the UV overlay
	 */
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|UVs", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	GetMeshPerVertexUVs( 
		UDynamicMesh* TargetMesh, 
		int UVSetIndex,
		FGeometryScriptUVList& UVList, 
		bool& bIsValidUVSet,
		bool& bHasVertexIDGaps,
		bool& bHasSplitUVs,
		UGeometryScriptDebug* Debug = nullptr);

};