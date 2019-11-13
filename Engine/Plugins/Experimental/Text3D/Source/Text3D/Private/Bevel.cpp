// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "Bevel.h"
#include "Data.h"
#include "Bevel/BevelLinear.h"
#include "Bevel/BevelType.h"
#include "Bevel/Part.h"

#include "Math/UnrealMathUtility.h"


class FTVectoriser;


void BevelContours(const TSharedPtr<FData> Data, const FTVectoriser& Vectoriser, const float Extrude, const float Bevel, EText3DBevelType Type, const int32 HalfCircleSegments, const int32 IterationsIn, const bool bHidePreviousIn, const int32 MarkedVertex, const int32 Segments, const int32 VisibleFaceIn)
{
	FBevelLinear BevelLinear(Data, Vectoriser, IterationsIn, bHidePreviousIn, Segments, VisibleFaceIn);

	if (Bevel > 0)
	{
		Data->SetCurrentMesh(EText3DMeshType::Bevel);
		switch (Type)
		{
		case EText3DBevelType::Linear:
		{
			const FVector2D Normal = FVector2D(1, -1).GetSafeNormal();
			BevelLinear.BevelContours(Bevel, Bevel, Normal, Normal, false, MarkedVertex);
			break;
		}
		case EText3DBevelType::HalfCircle:
		{
			const float Step = HALF_PI / HalfCircleSegments;

			float CosCurr = 1;
			float SinCurr = 0;

			float CosNext;
			float SinNext;

			float ExtrudeLocalNext;
			float ExpandLocalNext;

			bool bSmoothNext = false;

			FVector2D NormalNext;
			FVector2D NormalEnd;

			auto MakeStep = [&SinNext, &CosNext, Step, &ExtrudeLocalNext, &ExpandLocalNext, Bevel, &CosCurr, &SinCurr, &NormalNext](int32 Index)
			{
				FMath::SinCos(&SinNext, &CosNext, Index * Step);

				ExtrudeLocalNext = Bevel * (CosCurr - CosNext);
				ExpandLocalNext = Bevel * (SinNext - SinCurr);

				NormalNext = FVector2D(ExtrudeLocalNext, -ExpandLocalNext).GetSafeNormal();
			};

			MakeStep(1);
			for (int32 i = 0; i < HalfCircleSegments; i++)
			{
				CosCurr = CosNext;
				SinCurr = SinNext;

				const float ExtrudeLocal = ExtrudeLocalNext;
				const float ExpandLocal = ExpandLocalNext;

				const FVector2D Normal = NormalNext;
				FVector2D NormalStart;

				const bool bFirst = i == 0;
				const bool bLast = i == HalfCircleSegments - 1;

				const bool bSmooth = bSmoothNext;

				if (!bLast)
				{
					MakeStep(i + 2);
					bSmoothNext = FVector2D::DotProduct(Normal, NormalNext) >= -FPart::CosMaxAngle;
				}

				NormalStart = bFirst ? Normal : (bSmooth ? NormalEnd : Normal);
				NormalEnd = bLast ? Normal : (bSmoothNext ? (Normal + NormalNext).GetSafeNormal() : Normal);

				BevelLinear.BevelContours(ExtrudeLocal, ExpandLocal, NormalStart, NormalEnd, bSmooth, MarkedVertex);
			}

			break;
		}

		default:
			break;
		}
	}

#if !TEXT3D_WITH_INTERSECTION
	if (Bevel < Extrude / 2)
	{
		Data->SetCurrentMesh(EText3DMeshType::Extrude);
		BevelLinear.CreateExtrudeMesh(Extrude - (Bevel * 2.0f));
	}
#else
	if (Extrude > Bevel)
	{
		Data->SetCurrentMesh(EText3DMeshType::Extrude);
		const FVector2D Normal(1, 0);
		BevelLinear.BevelContours(Extrude - Bevel, 0, Normal, Normal, false, MarkedVertex);
	}

	Data->SetCurrentMesh(EText3DMeshType::Back);
	BevelLinear.CreateBackCap();
#endif
}
