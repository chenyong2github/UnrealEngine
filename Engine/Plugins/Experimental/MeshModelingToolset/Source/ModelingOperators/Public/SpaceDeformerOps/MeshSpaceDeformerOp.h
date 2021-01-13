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

	// Inputs
	TSharedPtr<const FDynamicMesh3> OriginalMesh;
	FFrame3d GizmoFrame;
	void SetTransform(const FTransform& Transform);

	//Converts the given modifier percent and returns the value between -1 and 1
	virtual double GetModifierValue() { return FMath::Lerp(0.01 * ModifierPercent, ModifierMin, ModifierMax); }

	// half the major axis of the bounding box for the source geometry
	double AxesHalfLength;

	//Percent input from the tool (between -100 and 100), as each operator will have a min/max value i.e. the angle of curvature, twist or scale. This is the percent used to interpolate between the min/max value
	double ModifierPercent;

	//The interval usually from [-1,0] upon which this operator will work. This corresponds to the region of space which this operator affects.
	double LowerBoundsInterval;

	//The interval usually from [0,1] upon which this operator will work. This corresponds to the region of space which this operator affects.
	double UpperBoundsInterval;

	/**
	 * Copies over the original mesh into result mesh, and initializes ObjectToGizmo in preparation to whatever work the base class does.
	 * Note that the function will return if OriginalMesh was null but doesn't have a way to log the error, so the base class should
	 * check OriginalMesh itself as well.
	 */
	virtual void CalculateResult(FProgressCancel* Progress) override;


protected:
	// transform, including translation, to gizmo space
	FMatrix ObjectToGizmo;

	//The minimum value provided by the constructor so that each operator can set its own interpolation min and max
	double ModifierMin{};

	//The minimum value provided by the constructor so that each operator can set its own interpolation min and max
	double ModifierMax{};
};