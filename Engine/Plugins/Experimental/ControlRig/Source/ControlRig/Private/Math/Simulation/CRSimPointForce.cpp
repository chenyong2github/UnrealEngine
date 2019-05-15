// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Math/Simulation/CRSimPointForce.h"

FVector FCRSimPointForce::Calculate(const FCRSimPoint& InPoint, float InDeltaTime) const
{
	FVector Force = FVector::ZeroVector;
	if(InPoint.Mass > SMALL_NUMBER)
	{
		switch(ForceType)
		{
			case ECRSimPointForceType::Direction:
			{
				if(bNormalize)
				{
					Force = Vector.GetSafeNormal();
				}
				else
				{
					Force = Vector;
				}
				Force = Force * Coefficient;
				break;
			}
		}
	}
	return Force;
}
