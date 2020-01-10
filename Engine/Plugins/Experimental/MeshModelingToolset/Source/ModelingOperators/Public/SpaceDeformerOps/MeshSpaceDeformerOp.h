// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"

class MODELINGOPERATORS_API FMeshSpaceDeformerOp : public FDynamicMeshOperator
{
public:
	FMeshSpaceDeformerOp(double Min, double Max) : ModifierMin{ Min }, ModifierMax{ Max }{};
	virtual ~FMeshSpaceDeformerOp() {}

	FDynamicMesh3* TargetMesh;

	// set ability on protected transform.

	//Converts the given modifier percent and returns the value between -1 and 1
	virtual double GetModifierValue() { return FMath::Lerp(0.01 * ModifierPercent, ModifierMin, ModifierMax); }

	/**
	* \brief Updates the pointer to the target mesh
	*/
	virtual void UpdateMesh(FDynamicMesh3* Mesh);


	/**
	* \brief Updates spatial data i.e. origin, orientation and other pertaining inputs needed by the operators
	* \param ObjectSpaceToOpSpaceTransform Rotation matrix to rotate from object space to the space expected by the operator
	* \param ObjectSpaceOrigin The location of the handle's origin, transformed from world space into the local coordinate system of the mesh
	* \param AxesHalfExtents Each element of this vector is a scalar corresponding to the distance from the ObjectSpaceOrigin (centroid) to the farthest vertex in that respective direction
	* \param LowerBounds The negative interval (only negative numbers) representing the range of effect in space this operator will have where -1 will utilize the entire half extent
	* \param UpperBounds The positive interval (only positive numbers) representing the range of effect in space this operator will have where 1 will utilize the entire half extent
	* \param ModifierValue Percent input from the tool (between -100 and 100), as each operator will have a min/max value i.e. the angle of curvature, twist or scale. This is the percent used to interpolate between the min/max value
	*/
	virtual void UpdateAxisData(const FMatrix3d& ObjectSpaceToOpSpaceTransform, const FVector3d& ObjectSpaceOrigin, const FVector3d& AxesHalfExtents, double LowerBounds, double UpperBounds, double ModifierValue);

	//Array of the original positions of each vertex of the mesh
	TArray<FVector3d> OriginalPositions;

	//Origin of the transformation, given in the local coordinate system of the mesh
	FVector3d AxisOriginObjectSpace;
	
	//Each element of this vector is a scalar corresponding to the distance from the ObjectSpaceOrigin (centroid) to the farthest vertex in that respective direction
	FVector3d AxesHalfLengths;

	//Rotation matrix to rotate from object space to the space expected by the operator
	FMatrix3d ObjectSpaceToOpSpace;

	//Precomputed inversion from the provided ObjectSpaceToOpSpace, used to convert back to the object's coordinate system
	FMatrix3d OpSpaceToObjectSpace;

	//Percent input from the tool (between -100 and 100), as each operator will have a min/max value i.e. the angle of curvature, twist or scale. This is the percent used to interpolate between the min/max value
	double ModifierPercent;

	//The minimum value provided by the constructor so that each operator can set it's own interpolation min and max
	double ModifierMin{};

	//The minimum value provided by the constructor so that each operator can set it's own interpolation min and max
	double ModifierMax{};

	//The interval usually from [-1,0] upon which this operator will work. This corresponds to the region of space which this operator affects.
	double LowerBoundsInterval;

	//The interval usually from [0,1] upon which this operator will work. This corresponds to the region of space which this operator affects.
	double UpperBoundsInterval;

	virtual void CalculateResult(FProgressCancel* Progress) override;


protected:

};