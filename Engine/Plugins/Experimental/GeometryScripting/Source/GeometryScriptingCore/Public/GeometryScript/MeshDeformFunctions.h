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
	/** Origin of the Bend Warp */
	UPROPERTY(BlueprintReadWrite, Category = Options)
	FVector BendOrigin = FVector(0,0,0);

	/** Bend will be along this axis, ie if bending a long tube, this axis would be aligned with the tube axis */
	UPROPERTY(BlueprintReadWrite, Category = Options)
	FVector BendAxis = FVector(0, 0, 1);

	/** Bend will bend towards this direction, eg if bending a vertical (+Z) bar with BendAxis = (0,0,1), A positive bend angle would bend the bar towards this direction (in the XY plane). Automatically calculated if this vector is Zero. */
	UPROPERTY(BlueprintReadWrite, Category = Options)
	FVector TowardAxis = FVector(0, 0, 0);

	/** Rotation in Degrees around the Bend Axis, this is applied after the TowardAxis rotation */
	UPROPERTY(BlueprintReadWrite, Category = Options)
	float BendAxisRotation = 0;

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
	/** Origin of the Bend Warp */
	UPROPERTY(BlueprintReadWrite, Category = Options)
	FVector TwistOrigin = FVector(0,0,0);

	/** Twist will be along this axis, ie if twisting a long tube, this the tube would twist "around" this axis */
	UPROPERTY(BlueprintReadWrite, Category = Options)
	FVector TwistAxis = FVector(0, 0, 1);

	/** Twist will begin pointed towards this direction (perpendicular to TwistAxis). Automatically calculated if this vector is Zero. */
	UPROPERTY(BlueprintReadWrite, Category = Options)
	FVector TowardAxis = FVector(0, 0, 0);

	/** Rotation in Degrees around the Twist Axis, this is applied after the TowardAxis rotation */
	UPROPERTY(BlueprintReadWrite, Category = Options)
	float TwistAxisRotation = 0;

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




UCLASS(meta = (ScriptName = "GeometryScript_MeshDeformers"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_MeshDeformFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Deformations")
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyBendWarpToMesh(  
		UDynamicMesh* TargetMesh, 
		FGeometryScriptBendWarpOptions Options,
		float BendAngle = 45,
		float BendExtent = 50,
		UGeometryScriptDebug* Debug = nullptr);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Deformations")
	static UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyTwistWarpToMesh(  
		UDynamicMesh* TargetMesh, 
		FGeometryScriptTwistWarpOptions Options,
		float TwistAngle = 45,
		float TwistExtent = 50,
		UGeometryScriptDebug* Debug = nullptr);


};