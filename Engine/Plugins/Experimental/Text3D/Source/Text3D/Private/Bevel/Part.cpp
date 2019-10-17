// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Bevel/Part.h"
#include "Bevel/Util.h"

#include "HAL/PlatformMath.h"


const float FPart::CosMaxAngle = -0.9f;

void FPart::ResetDoneExpand()
{
	DoneExpand = 0;
}

void FPart::ComputeTangentX()
{
	TangentX = (Next->Position - Position).GetSafeNormal();
}

void FPart::ComputeNormalAndSmooth()
{
	const FVector2D a = -Prev->TangentX;
	const FVector2D c = TangentX;

	Normal = a + c;

	const float NormalLength2 = Normal.SizeSquared();

	// Scale is needed to make ((p_(i+1) + k * n_(i+1)) - (p_i + k * n_i)) parallel to (p_(i+1) - p_i). Also (k) is distance between original edge and this edge after expansion with value (k).
	const float Scale = -FPlatformMath::Sqrt(2 / (1 - FVector2D::DotProduct(a, c)));

	// If previous and next edge are nearly on one line
	if (FMath::IsNearlyZero(NormalLength2, 0.0001f))
	{
		Normal = FVector2D(a.Y, -a.X) * Scale;
	}
	else
	{
		// Sign of cross product is needed to be sure that Normal is directed outside.
		Normal *= -Scale * FPlatformMath::Sign(FVector2D::CrossProduct(a, c)) / FPlatformMath::Sqrt(NormalLength2);
	}

	bSmooth = FVector2D::DotProduct(-Prev->TangentX, TangentX) <= CosMaxAngle;
}

void FPart::ResetInitialPosition()
{
	InitialPosition = Position;
}

void FPart::ComputeInitialPosition()
{
	InitialPosition = Position - DoneExpand * Normal;
}

void FPart::DecreaseExpandsFar(const float Delta)
{
	for (auto i = AvailableExpandsFar.CreateIterator(); i; ++i)
	{
		i->Value -= Delta;

		if (i->Value < 0)
		{
			i.RemoveCurrent();
		}
	}
}

FVector2D FPart::Expanded(const float Value) const
{
	return Position + Normal * Value;
}
