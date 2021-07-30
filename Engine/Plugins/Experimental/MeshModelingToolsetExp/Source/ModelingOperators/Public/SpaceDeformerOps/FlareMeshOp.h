// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshSpaceDeformerOp.h"

namespace UE
{
namespace Geometry
{

class MODELINGOPERATORS_API FFlareMeshOp : public FMeshSpaceDeformerOp
{
public:
	virtual void CalculateResult(FProgressCancel* Progress) override;

	/** 0% does nothing, 100% moves 2x away from Z axis at extremal point, -100% squishes down to Z axis at extremal point. */
	double FlarePercentX = 100;
	double FlarePercentY = 100;

	/** 
	 * Changes the type of flaring. When false, the flaring is the curve of sin(y) from 0 to Pi,
	 * which makes the ends of the flaring sharp because the derivative is not 0. When true, the 
	 * flaring is the curve of cos(y) + 1 from -Pi to Pi, which makes the ends smooth out back
	 * into the shape.
	 */
	bool bSmoothEnds = false;
protected:

};

} // end namespace UE::Geometry
} // end namespace UE
