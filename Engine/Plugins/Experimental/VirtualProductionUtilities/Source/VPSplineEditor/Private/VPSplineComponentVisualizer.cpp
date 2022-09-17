// Copyright Epic Games, Inc. All Rights Reserved.

#include "VPSplineComponentVisualizer.h"
#include "Internationalization/Text.h"
#include "Math/Color.h"
#include "CanvasTypes.h"
#include "CanvasItem.h"

#define LOCTEXT_NAMESPACE "SplineComponentVisualizer"

FVPSplineComponentVisualizer::FVPSplineComponentVisualizer()
	: FSplineComponentVisualizer()
{
}

FVPSplineComponentVisualizer::~FVPSplineComponentVisualizer()
{
}
void FVPSplineComponentVisualizer::OnRegister()
{
	FSplineComponentVisualizer::OnRegister();
}

void FVPSplineComponentVisualizer::DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	FSplineComponentVisualizer::DrawVisualizationHUD(Component, Viewport, View, Canvas);
	if (Canvas == nullptr || View == nullptr)
	{
		return;
	}

	const FIntRect CanvasRect = Canvas->GetViewRect();
	float HalfX = CanvasRect.Width() / 2.f;
	float HalfY = CanvasRect.Height() / 2.f;


	if (const UVPSplineComponent* SplineComp = Cast<const UVPSplineComponent>(Component))
	{
		if (UVPSplineMetadata* Metadata = SplineComp->VPSplineMetadata)
		{
			int32 NumOfPoints = SplineComp->GetNumberOfSplinePoints();
			for (int32 i = 0; i < NumOfPoints; ++i)
			{
				FVector Location = SplineComp->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
				FPlane Plane(0, 0, 0, 0);
				Plane = View->Project(Location);
				const FVector Position(Plane);
				const float DrawPositionX = FMath::FloorToFloat(HalfX + Position.X * HalfX);
				const float DrawPositionY = FMath::FloorToFloat(HalfY + -1.f * Position.Y * HalfY);

				FNumberFormattingOptions FmtOptions;
				FmtOptions.SetMaximumFractionalDigits(3);
				const FText Text = FText::AsNumber(Metadata->NormalizedPosition.Points[i].OutVal, &FmtOptions);
				Canvas->DrawShadowedString(DrawPositionX, DrawPositionY, *Text.ToString(), GEngine->GetLargeFont(), FLinearColor::Yellow, FLinearColor::Black);
			}
		}
		

	}
}

#undef LOCTEXT_NAMESPACE