// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Bevel/Part.h"
#include "Bevel/Util.h"

#include "HAL/PlatformMath.h"


const float FPart::CosMaxAngle = -0.9f;

FPart::FPart()
{
	Prev = nullptr;
	Next = nullptr;
	DoneExpand = 0.0f;
	bSmooth = true;
	AvailableExpandNear = 0.0f;
}

void FPart::ResetDoneExpand()
{
	DoneExpand = 0;
}

void FPart::ComputeTangentX()
{
	check(Next);

	TangentX = (Next->Position - Position).GetSafeNormal();
}

bool FPart::ComputeNormalAndSmooth()
{
	check(Prev);

	const FVector2D A = -Prev->TangentX;
	const FVector2D C = TangentX;

	Normal = A + C;

	const float NormalLength2 = Normal.SizeSquared();

	// Scale is needed to make ((p_(i+1) + k * n_(i+1)) - (p_i + k * n_i)) parallel to (p_(i+1) - p_i). Also (k) is distance between original edge and this edge after expansion with value (k).
	const float OneMinusADotC = 1.0f - FVector2D::DotProduct(A, C);

	if (FMath::IsNearlyZero(OneMinusADotC))
	{
		return false;
	}

	const float Scale = -FPlatformMath::Sqrt(2.0f / OneMinusADotC);


	// If previous and next edge are nearly on one line
	if (FMath::IsNearlyZero(NormalLength2, 0.0001f))
	{
		Normal = FVector2D(A.Y, -A.X) * Scale;
	}
	else
	{
		// Sign of cross product is needed to be sure that Normal is directed outside.
		Normal *= -Scale * FPlatformMath::Sign(FVector2D::CrossProduct(A, C)) / FPlatformMath::Sqrt(NormalLength2);
	}

	bSmooth = FVector2D::DotProduct(-Prev->TangentX, TangentX) <= CosMaxAngle;
	return true;
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
