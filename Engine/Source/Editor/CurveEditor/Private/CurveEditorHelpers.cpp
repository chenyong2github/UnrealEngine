// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CurveEditorHelpers.h"


namespace CurveEditor
{

FVector2D ComputeScreenSpaceTangentOffset(const FCurveEditorScreenSpace& CurveSpace, float Tangent, float Weight)
{
	const float Angle = FMath::Atan(-Tangent);
	FVector2D Offset;
	FMath::SinCos(&Offset.Y, &Offset.X, Angle);
	Offset *= Weight;

	Offset.X *= CurveSpace.PixelsPerInput();
	Offset.Y *= CurveSpace.PixelsPerOutput();
	return Offset;
}

void TangentAndWeightFromOffset(const FCurveEditorScreenSpace& CurveSpace, const FVector2D& TangentOffset, float& OutTangent, float& OutWeight)
{
	float X = CurveSpace.ScreenToSeconds(TangentOffset.X) - CurveSpace.ScreenToSeconds(0);
	float Y = CurveSpace.ScreenToValue(TangentOffset.Y) - CurveSpace.ScreenToValue(0);

	OutTangent = Y / X;
	OutWeight = FMath::Sqrt(X*X + Y*Y);
}

FVector2D GetVectorFromSlopeAndLength(float Slope, float Length)
{
	float x = Length / FMath::Sqrt(Slope*Slope + 1.f);
	float y = Slope * x;
	return FVector2D(x, y);
}

void ConstructYGridLines(const FCurveEditorScreenSpace& ViewSpace, uint8 InMinorDivisions, TArray<float>& OutMajorGridLines, TArray<float>& OutMinorGridLines, FText GridLineLabelFormatY, TArray<FText>* OutMajorGridLabels)
{
	const float GridPixelSpacing = ViewSpace.GetPhysicalHeight() / 5.f;

	const float Order = FMath::Pow(10.f, FMath::FloorToInt(FMath::LogX(10.f, GridPixelSpacing / ViewSpace.PixelsPerOutput())));

	static const int32 DesirableBases[]  = { 2, 5 };
	static const int32 NumDesirableBases = UE_ARRAY_COUNT(DesirableBases);

	const int32 Scale = FMath::RoundToInt(GridPixelSpacing / ViewSpace.PixelsPerOutput() / Order);
	int32 Base = DesirableBases[0];
	for (int32 BaseIndex = 1; BaseIndex < NumDesirableBases; ++BaseIndex)
	{
		if (FMath::Abs(Scale - DesirableBases[BaseIndex]) < FMath::Abs(Scale - Base))
		{
			Base = DesirableBases[BaseIndex];
		}
	}

	double MajorGridStep = FMath::Pow(Base, FMath::FloorToFloat(FMath::LogX(Base, Scale))) * Order;

	const double FirstMajorLine = FMath::FloorToDouble(ViewSpace.GetOutputMin() / MajorGridStep) * MajorGridStep;
	const double LastMajorLine = FMath::CeilToDouble(ViewSpace.GetOutputMax() / MajorGridStep) * MajorGridStep;

	FNumberFormattingOptions FormattingOptions;
	FormattingOptions.SetMaximumFractionalDigits(6);

	for (double CurrentMajorLine = FirstMajorLine; CurrentMajorLine <= LastMajorLine; CurrentMajorLine += MajorGridStep)
	{
		OutMajorGridLines.Add(ViewSpace.ValueToScreen(CurrentMajorLine));
		if (OutMajorGridLabels)
		{
			OutMajorGridLabels->Add(FText::Format(GridLineLabelFormatY, FText::AsNumber(CurrentMajorLine, &FormattingOptions)));
		}

		for (int32 Step = 1; Step < InMinorDivisions; ++Step)
		{
			OutMinorGridLines.Add(ViewSpace.ValueToScreen(CurrentMajorLine + Step * MajorGridStep / InMinorDivisions));
		}
	}
}

} // namespace CurveEditor