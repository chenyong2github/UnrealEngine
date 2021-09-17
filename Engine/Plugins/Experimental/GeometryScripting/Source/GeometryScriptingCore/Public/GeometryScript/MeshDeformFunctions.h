// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "MeshDeformFunctions.generated.h"

class UDynamicMesh;


USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptBendWarpOptions
{
	GENERATED_BODY()
public:
	/** Symmetric extents are [-BendExtent,BendExtent], if disabled, then [-LowerExtent,BendExtent] is used  */
	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bSymmetricExtents = true;

	/** Lower extent used when bSymmetricExtents = false */
	UPROPERTY(BlueprintReadWrite, Category = Options, meta = (EditCondition = "bSymmetricExtents == false"))
	float LowerExtent = 10;

	/** If true, the Bend is "centered" at the Origin, ie the regions on either side of the extents are rigidly transformed. If false, the Bend begins at the start of the Lower Extents, and the "lower" region is not affected. */
	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bBidirectional = true;
};



USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptTwistWarpOptions
{
	GENERATED_BODY()
public:
	/** Symmetric extents are [-BendExtent,BendExtent], if disabled, then [-LowerExtent,BendExtent] is used  */
	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bSymmetricExtents = true;

	/** Lower extent used when bSymmetricExtents = false */
	UPROPERTY(BlueprintReadWrite, Category = Options, meta = (EditCondition = "bSymmetricExtents == false"))
	float LowerExtent = 10;

	/** If true, the Twist is "centered" at the Origin, ie the regions on either side of the extents are rigidly transformed. If false, the Twist begins at the start of the Lower Extents, and the "lower" region is not affected. */
	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bBidirectional = true;
};

UENUM(BlueprintType)
enum class EGeometryScriptFlareType : uint8
{
	//Displaced by sin(pi x) with x in 0 to 1
	SinMode = 0,

	//Displaced by sin(pi x)*sin(pi x) with x in 0 to 1. This provides a smooth normal transition.
	SinSquaredMode = 1,

	// Displaced by piecewise-linear trianglular mode
	TriangleMode = 2
};

USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptFlareWarpOptions
{
	GENERATED_BODY()
public:
	/** Symmetric extents are [-BendExtent,BendExtent], if disabled, then [-LowerExtent,BendExtent] is used  */
	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bSymmetricExtents = true;

	/** Lower extent used when bSymmetricExtents = false */
	UPROPERTY(BlueprintReadWrite, Category = Options, meta = (EditCondition = "bSymmetricExtents == false"))
	float LowerExtent = 10;

	/** Determines the profile used as a displacement */
	UPROPERTY(BlueprintReadWrite, Category = Options)
	EGeometryScriptFlareType FlareType = EGeometryScriptFlareType::SinMode;
};



USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptPerlinNoiseLayerOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	float Magnitude = 5.0;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	float Frequency = 0.25;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	FVector FrequencyShift = FVector::Zero();

	UPROPERTY(BlueprintReadWrite, Category = Options)
	int RandomSeed = 0;
};



UENUM(BlueprintType)
enum class EGeometryScriptMathWarpType : uint8
{
	SinWave1D = 0,
	SinWave2D = 1,
	SinWave3D = 2
};


USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptMathWarpOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	float Magnitude = 5.0f;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	float Frequency = 0.25f;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	float FrequencyShift = 0.0;
};




USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptPerlinNoiseOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	FGeometryScriptPerlinNoiseLayerOptions BaseLayer;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bApplyAlongNormal = true;
};


USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptIterativeMeshSmoothingOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	int NumIterations = 10;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	float Alpha = 0.2;
};




USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptDisplaceFromTextureOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	float Magnitude = 1.0f;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	FVector2D UVScale = FVector2D(1,1);

	UPROPERTY(BlueprintReadWrite, Category = Options)
	FVector2D UVOffset = FVector2D(0,0);

	UPROPERTY(BlueprintReadWrite, Category = Options)
	float Center = 0.5;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	int ImageChannel = 0;
};





UCLASS(meta = (ScriptName = "GeometryScript_MeshDeformers"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_MeshDeformFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Deformations", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyBendWarpToMesh(  
		UDynamicMesh* TargetMesh, 
		FGeometryScriptBendWarpOptions Options,
		FTransform BendOrientation,
		float BendAngle = 45,
		float BendExtent = 50,
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Deformations", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyTwistWarpToMesh(  
		UDynamicMesh* TargetMesh, 
		FGeometryScriptTwistWarpOptions Options,
		FTransform TwistOrientation,
		float TwistAngle = 45,
		float TwistExtent = 50,
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Deformations", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyFlareWarpToMesh(  
		UDynamicMesh* TargetMesh, 
		FGeometryScriptFlareWarpOptions Options,
		FTransform FlareOrientation,
		float FlarePercentX = 0,
		float FlarePercentY = 0,
		float FlareExtent = 50,
		UGeometryScriptDebug* Debug = nullptr);


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Deformations", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyMathWarpToMesh(  
		UDynamicMesh* TargetMesh, 
		FTransform WarpOrientation,
		EGeometryScriptMathWarpType WarpType,
		FGeometryScriptMathWarpOptions Options,
		UGeometryScriptDebug* Debug = nullptr);


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Deformations", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyPerlinNoiseToMesh(  
		UDynamicMesh* TargetMesh, 
		FGeometryScriptPerlinNoiseOptions Options,
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Deformations", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyIterativeSmoothingToMesh(  
		UDynamicMesh* TargetMesh, 
		FGeometryScriptIterativeMeshSmoothingOptions Options,
		UGeometryScriptDebug* Debug = nullptr);


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Deformations", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyDisplaceFromTextureMap(  
		UDynamicMesh* TargetMesh, 
		UTexture2D* Texture,
		FGeometryScriptDisplaceFromTextureOptions Options,
		int32 UVLayer = 0,
		UGeometryScriptDebug* Debug = nullptr);

	
};