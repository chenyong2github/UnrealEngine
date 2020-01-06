// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	virtual void CopySource(const FDynamicMesh3& Mesh, const FTransform& XFrom);


	// half the major axis of the bounding box for the source geometry
	double AxesHalfLength;

	// transform, including translation, to gizmo space
	FMatrix ObjectToGizmo;

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