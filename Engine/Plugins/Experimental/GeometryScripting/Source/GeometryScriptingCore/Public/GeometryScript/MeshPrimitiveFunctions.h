// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "MeshPrimitiveFunctions.generated.h"


UENUM(BlueprintType)
enum class EGeometryScriptPrimitivePolygroupMode : uint8
{
	SingleGroup = 0,
	PerFace = 1,
	PerQuad = 2
};



USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptPrimitiveOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options)
	EGeometryScriptPrimitivePolygroupMode PolygroupMode = EGeometryScriptPrimitivePolygroupMode::PerFace;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	bool bFlipOrientation = false;
};


UCLASS(meta = (ScriptName = "GeometryScript_Primitives"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_MeshPrimitiveFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Primitives", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AppendBox( 
		UDynamicMesh* TargetMesh, 
		FGeometryScriptPrimitiveOptions PrimitiveOptions,
		FTransform Transform,
		FBox Box,
		int32 StepsX = 0,
		int32 StepsY = 0,
		int32 StepsZ = 0,
		UGeometryScriptDebug* Debug = nullptr);


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Primitives", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AppendSphereLatLong( 
		UDynamicMesh* TargetMesh, 
		FGeometryScriptPrimitiveOptions PrimitiveOptions,
		FTransform Transform,
		float Radius = 50,
		int32 StepsPhi = 8,
		int32 StepsTheta = 8,
		UGeometryScriptDebug* Debug = nullptr);


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Primitives", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AppendSphereBox( 
		UDynamicMesh* TargetMesh, 
		FGeometryScriptPrimitiveOptions PrimitiveOptions,
		FTransform Transform,
		float Radius = 50,
		int32 StepsX = 6,
		int32 StepsY = 6,
		int32 StepsZ = 6,
		UGeometryScriptDebug* Debug = nullptr);


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Primitives", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AppendCapsule( 
		UDynamicMesh* TargetMesh, 
		FGeometryScriptPrimitiveOptions PrimitiveOptions,
		FTransform Transform,
		float Radius = 10,
		float LineLength = 50,
		int32 HemisphereSteps = 4,
		int32 CircleSteps = 8,
		UGeometryScriptDebug* Debug = nullptr);


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Primitives", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AppendCylinder( 
		UDynamicMesh* TargetMesh, 
		FGeometryScriptPrimitiveOptions PrimitiveOptions,
		FTransform Transform,
		float Radius = 25,
		float Height = 50,
		int32 RadialSteps = 8,
		int32 HeightSteps = 0,
		bool bCapped = true,
		UGeometryScriptDebug* Debug = nullptr);


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Primitives", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AppendCone( 
		UDynamicMesh* TargetMesh, 
		FGeometryScriptPrimitiveOptions PrimitiveOptions,
		FTransform Transform,
		float BaseRadius = 25,
		float TopRadius = 5,
		float Height = 50,
		int32 RadialSteps = 8,
		int32 HeightSteps = 0,
		bool bCapped = true,
		UGeometryScriptDebug* Debug = nullptr);


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Primitives", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AppendTorus( 
		UDynamicMesh* TargetMesh, 
		FGeometryScriptPrimitiveOptions PrimitiveOptions,
		FTransform Transform,
		float MajorRadius = 100,
		float MinorRadius = 20,
		int32 MajorSteps = 8,
		int32 MinorSteps = 4,
		UGeometryScriptDebug* Debug = nullptr);

	/**
	 * In the coordinate system of the revolve polygon, +X is towards the "outside" of the revolve donut, and +Y is "up" (ie +Z in local space)
	 * Polygon should be oriented counter-clockwise to produce a correctly-oriented shape, otherwise it will be inside-out
	 * Polygon endpoint is not repeated.
	 */
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Primitives", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AppendSimpleRevolvePolygon( 
		UDynamicMesh* TargetMesh, 
		FGeometryScriptPrimitiveOptions PrimitiveOptions,
		FTransform Transform,
		const TArray<FVector2D>& PolygonVertices,
		float Radius = 100,
		int32 Steps = 8,
		UGeometryScriptDebug* Debug = nullptr);

	/**
	 * Polygon should be oriented counter-clockwise to produce a correctly-oriented shape, otherwise it will be inside-out
	 * Polygon endpoint is not repeated.
	 */
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Primitives", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AppendSimpleExtrudePolygon( 
		UDynamicMesh* TargetMesh, 
		FGeometryScriptPrimitiveOptions PrimitiveOptions,
		FTransform Transform,
		const TArray<FVector2D>& PolygonVertices,
		float Height = 100,
		int32 HeightSteps = 0,
		bool bCapped = true,
		UGeometryScriptDebug* Debug = nullptr);


	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Primitives", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AppendSimpleSweptPolygon( 
		UDynamicMesh* TargetMesh, 
		FGeometryScriptPrimitiveOptions PrimitiveOptions,
		FTransform Transform,
		const TArray<FVector2D>& PolygonVertices,
		const TArray<FVector>& SweepPath,
		bool bLoop = false,
		bool bCapped = true,
		float StartScale = 1.0f,
		float EndScale = 1.0f,
		UGeometryScriptDebug* Debug = nullptr);



	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Primitives", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AppendRectangle( 
		UDynamicMesh* TargetMesh, 
		FGeometryScriptPrimitiveOptions PrimitiveOptions,
		FTransform Transform,
		FBox2D Box,
		int32 StepsWidth = 0,
		int32 StepsHeight = 0,
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Primitives", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AppendRoundRectangle( 
		UDynamicMesh* TargetMesh, 
		FGeometryScriptPrimitiveOptions PrimitiveOptions,
		FTransform Transform,
		FBox2D Box,
		float CornerRadius = 5,
		int32 StepsWidth = 0,
		int32 StepsHeight = 0,
		int32 StepsRound = 4,
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Primitives", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AppendDisc( 
		UDynamicMesh* TargetMesh, 
		FGeometryScriptPrimitiveOptions PrimitiveOptions,
		FTransform Transform,
		float Radius = 50,
		int32 AngleSteps = 16,
		int32 SpokeSteps = 0,
		float StartAngle = 0,
		float EndAngle = 360,
		float HoleRadius = 0,
		UGeometryScriptDebug* Debug = nullptr);

	/**
	* Polygon should be oriented counter-clockwise to produce a correctly-oriented shape, otherwise it will be inside-out
	* Polygon endpoint is not repeated.
	*/
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Primitives", meta=(ScriptMethod))
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	AppendTriangulatedPolygon( 
		UDynamicMesh* TargetMesh, 
		FGeometryScriptPrimitiveOptions PrimitiveOptions,
		FTransform Transform,
		const TArray<FVector2D>& PolygonVertices,
		bool bAllowSelfIntersections = true,
		UGeometryScriptDebug* Debug = nullptr);

};