// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModulationPatchCurveEditorViewStacked.h"

#include "AudioModulationStyle.h"
#include "Curves/CurveFloat.h"
#include "CurveEditor.h"
#include "CurveModel.h"
#include "EditorStyleSet.h"
#include "Fonts/FontMeasure.h"
#include "IAudioModulation.h"
#include "SCurveEditorPanel.h"
#include "Sound/SoundModulationDestination.h"
#include "SoundControlBus.h"
#include "SoundModulationPatch.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "ModulationPatchEditor"

namespace PatchCurveViewUtils
{
	void FormatLabel(const USoundModulationParameter& InParameter, const FNumberFormattingOptions& InNumFormatOptions, FText& InOutLabel)
	{
		const float LinearValue = FCString::Atof(*InOutLabel.ToString());
		const float UnitValue = InParameter.ConvertLinearToUnit(LinearValue);
		FText UnitLabel = FText::AsNumber(UnitValue, &InNumFormatOptions);
		InOutLabel = FText::Format(LOCTEXT("ModulationPatchCurveView_UnitFormat", "{0} ({1})"), UnitLabel, InOutLabel);
	}
} // namespace PatchCurveViewUtils



FModPatchCurveEditorModel::FModPatchCurveEditorModel(FRichCurve& InRichCurve, UObject* InOwner, EModPatchOutputEditorCurveSource InSource, UCurveFloat* InSharedCurve)
	: FRichCurveEditorModelRaw(&InRichCurve, InOwner)
	, Patch(CastChecked<USoundModulationPatch>(InOwner))
	, Source(InSource)
{
	check(InOwner);

	FText InputAxisName = LOCTEXT("ModulationCurveDisplayName_Linear", "Linear");
	FText OutputAxisName = InputAxisName;
	if (Patch.IsValid())
	{
		if (Patch->PatchSettings.InputParameter)
		{
			InputAxisName = Patch->PatchSettings.InputParameter->Settings.UnitDisplayName;
		}

		if (Patch->PatchSettings.OutputParameter)
		{
			OutputAxisName = Patch->PatchSettings.OutputParameter->Settings.UnitDisplayName;
		}
	}

	const FText ShortNameBase = FText::Format(LOCTEXT("ModulationCurveDisplayName_Axis", "X = {0}, Y = {1}"), InputAxisName, OutputAxisName);

	bKeyDrawEnabled = true;
	Color			= UAudioModulationStyle::GetControlBusColor();
	IntentionName	= TEXT("AudioControlValue");
	SupportedViews	= ViewId;

	const bool bIsBypassed = GetIsBypassed();
	if (bIsBypassed)
	{
		SetShortDisplayName(FText::Format(LOCTEXT("ModulationCurveDisabledDisplayName", "{0} (Bypassed)"), ShortNameBase));
		bKeyDrawEnabled = false;
	}
	else
	{
		switch (Source)
		{
			case EModPatchOutputEditorCurveSource::Shared:
			{
				check(InSharedCurve);
				FText CurveNameText = FText::FromString(InSharedCurve->GetName());
				SetShortDisplayName(FText::Format(LOCTEXT("ModulationSharedDisplayName", "{0} [Shared ({1})]"), ShortNameBase, CurveNameText));
			}
			break;

			case EModPatchOutputEditorCurveSource::Custom:
			{
				ShortDisplayName = FText::Format(LOCTEXT("ModulationOutputCurveDisplayName", "{0} [Custom]"), ShortNameBase);
			}
			break;

			case EModPatchOutputEditorCurveSource::Expression:
			{
				bKeyDrawEnabled = false;
				ShortDisplayName = FText::Format(LOCTEXT("ModulationOutputCurveExpressionDisplayName", "{0} [Expression]"), ShortNameBase);
			}
			break;

			case EModPatchOutputEditorCurveSource::Unset:
			default:
			{
				bKeyDrawEnabled = false;
				ShortDisplayName = FText::Format(LOCTEXT("ModulationOutputCurveUnsetDisplayName", "{0} [Shared (Unset)]"), ShortNameBase);
			}
			break;
		}
	}
}

FLinearColor FModPatchCurveEditorModel::GetColor() const
{
	return !GetIsBypassed() && (Source == EModPatchOutputEditorCurveSource::Custom || Source == EModPatchOutputEditorCurveSource::Expression)
		? Color
		: Color.Desaturate(0.45f);
}

bool FModPatchCurveEditorModel::GetIsBypassed() const
{
	if (Patch.IsValid())
	{
		return Patch->PatchSettings.bBypass;
	}

	return true;
}

EModPatchOutputEditorCurveSource FModPatchCurveEditorModel::GetSource() const
{
	return Source;
}

const USoundModulationPatch* FModPatchCurveEditorModel::GetPatch() const
{
	return Patch.Get();
}

bool FModPatchCurveEditorModel::IsReadOnly() const
{
	return Source != EModPatchOutputEditorCurveSource::Custom;
}

ECurveEditorViewID FModPatchCurveEditorModel::ViewId = ECurveEditorViewID::Invalid;

void SModulationPatchEditorViewStacked::Construct(const FArguments& InArgs, TWeakPtr<FCurveEditor> InCurveEditor)
{
	SCurveEditorViewStacked::Construct(InArgs, InCurveEditor);

	StackedHeight = 250.0;
	StackedPadding = 28.0f;
}

void SModulationPatchEditorViewStacked::PaintView(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (CurveEditor)
	{
		const ESlateDrawEffect DrawEffects = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

		DrawBackground(AllottedGeometry, OutDrawElements, BaseLayerId, DrawEffects);
		DrawViewGrids(AllottedGeometry, MyCullingRect, OutDrawElements, BaseLayerId, DrawEffects);
		DrawLabels(AllottedGeometry, MyCullingRect, OutDrawElements, BaseLayerId, DrawEffects);
		DrawCurves(CurveEditor.ToSharedRef(), AllottedGeometry, MyCullingRect, OutDrawElements, BaseLayerId, InWidgetStyle, DrawEffects);
	}
}

void SModulationPatchEditorViewStacked::DrawLabels(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, ESlateDrawEffect DrawEffects) const
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (!CurveEditor)
	{
		return;
	}

	const int32 LabelLayerId = BaseLayerId + CurveViewConstants::ELayerOffset::Labels;

	const double ValuePerPixel = 1.0 / StackedHeight;
	const double ValueSpacePadding = StackedPadding * ValuePerPixel;

	const FSlateFontInfo FontInfo = FCoreStyle::Get().GetFontStyle("FontAwesome.11");
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const FCurveEditorScreenSpaceV ViewSpace = GetViewSpace();

	// Draw the curve labels for each view
	for (auto It = CurveInfoByID.CreateConstIterator(); It; ++It)
	{
		FCurveModel* Curve = CurveEditor->FindCurve(It.Key());
		if (!ensureAlways(Curve))
		{
			continue;
		}

		const int32  CurveIndexFromBottom = CurveInfoByID.Num() - It->Value.CurveIndex - 1;
		const double PaddingToBottomOfView = (CurveIndexFromBottom + 1)*ValueSpacePadding;

		const float PixelBottom = ViewSpace.ValueToScreen(CurveIndexFromBottom + PaddingToBottomOfView);
		const float PixelTop = ViewSpace.ValueToScreen(CurveIndexFromBottom + PaddingToBottomOfView + 1.0);

		if (!FSlateRect::DoRectanglesIntersect(MyCullingRect, TransformRect(AllottedGeometry.GetAccumulatedLayoutTransform(), FSlateRect(0, PixelTop, LocalSize.X, PixelBottom))))
		{
			continue;
		}

		const FText Label = Curve->GetLongDisplayName();

		const FVector2D Position(CurveViewConstants::CurveLabelOffsetX * 1.5f, PixelTop + CurveViewConstants::CurveLabelOffsetY * 3.0f);

		const FPaintGeometry LabelGeometry = AllottedGeometry.ToPaintGeometry(FSlateLayoutTransform(Position));
		const FPaintGeometry LabelDropshadowGeometry = AllottedGeometry.ToPaintGeometry(FSlateLayoutTransform(Position + FVector2D(2, 2)));

		// Drop shadow
		FSlateDrawElement::MakeText(OutDrawElements, LabelLayerId, LabelDropshadowGeometry, Label, FontInfo, DrawEffects, FLinearColor::Black.CopyWithNewOpacity(0.80f));
		FSlateDrawElement::MakeText(OutDrawElements, LabelLayerId + 1, LabelGeometry, Label, FontInfo, DrawEffects, Curve->GetColor());
	}
}

void SModulationPatchEditorViewStacked::DrawViewGrids(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, ESlateDrawEffect DrawEffects) const
{
	TSharedPtr<FCurveEditor> CurveEditor = WeakCurveEditor.Pin();
	if (!CurveEditor)
	{
		return;
	}

	// Rendering info
	const float          Width = AllottedGeometry.GetLocalSize().X;
	const float          Height = AllottedGeometry.GetLocalSize().Y;
	const FSlateBrush*   WhiteBrush = FEditorStyle::GetBrush("WhiteBrush");

	FModPatchViewGridDrawInfo DrawInfo(&AllottedGeometry, GetViewSpace(), CurveEditor->GetPanel()->GetGridLineTint(), BaseLayerId);

	TArray<float> MajorGridLinesX, MinorGridLinesX;
	TArray<FText> MajorGridLabelsX;

	GetGridLinesX(CurveEditor.ToSharedRef(), MajorGridLinesX, MinorGridLinesX, &MajorGridLabelsX);

	const double ValuePerPixel = 1.0 / StackedHeight;
	const double ValueSpacePadding = StackedPadding * ValuePerPixel;

	for (auto It = CurveInfoByID.CreateConstIterator(); It; ++It)
	{
		DrawInfo.SetCurveModel(CurveEditor->FindCurve(It.Key()));
		if (!ensureAlways(DrawInfo.GetCurveModel()))
		{
			continue;
		}

		TArray<FText> CurveModelGridLabelsX = MajorGridLabelsX;
		check(MajorGridLinesX.Num() == CurveModelGridLabelsX.Num());

		if (const FModPatchCurveEditorModel* EditorModel = static_cast<const FModPatchCurveEditorModel*>(DrawInfo.GetCurveModel()))
		{
			if (const USoundModulationPatch* Patch = EditorModel->GetPatch())
			{
				for (int32 i = 0; i < CurveModelGridLabelsX.Num(); ++i)
				{
					FText& Label = CurveModelGridLabelsX[i];
					if (USoundModulationParameter* InputParameter = Patch->PatchSettings.InputParameter)
					{
						PatchCurveViewUtils::FormatLabel(*InputParameter, DrawInfo.LabelFormat, Label);
					}
				}
			}
		}

		const int32  Index = CurveInfoByID.Num() - It->Value.CurveIndex - 1;
		double Padding = (Index + 1) * ValueSpacePadding;
		DrawInfo.SetLowerValue(Index + Padding);

		FSlateRect BoundsRect(0, DrawInfo.GetPixelTop(), Width, DrawInfo.GetPixelBottom());
		if (!FSlateRect::DoRectanglesIntersect(MyCullingRect, TransformRect(AllottedGeometry.GetAccumulatedLayoutTransform(), BoundsRect)))
		{
			continue;
		}

		// Tint the views based on their curve color
		{
			FLinearColor CurveColorTint = DrawInfo.GetCurveModel()->GetColor().CopyWithNewOpacity(0.05f);
			const FPaintGeometry BoxGeometry = AllottedGeometry.ToPaintGeometry(
				FVector2D(Width, StackedHeight),
				FSlateLayoutTransform(FVector2D(0.f, DrawInfo.GetPixelTop()))
			);

			const int32 GridOverlayLayerId = DrawInfo.GetBaseLayerId() + CurveViewConstants::ELayerOffset::GridOverlays;
			FSlateDrawElement::MakeBox(OutDrawElements, GridOverlayLayerId, BoxGeometry, WhiteBrush, DrawEffects, CurveColorTint);
		}

		// Horizontal grid lines
		DrawInfo.LinePoints[0].X = 0.0;
		DrawInfo.LinePoints[1].X = Width;

		DrawViewGridLineX(OutDrawElements, DrawInfo, DrawEffects, 1.0  /* OffsetAlpha */, true  /* bIsMajor */);
		DrawViewGridLineX(OutDrawElements, DrawInfo, DrawEffects, 0.75 /* OffsetAlpha */, false /* bIsMajor */);
		DrawViewGridLineX(OutDrawElements, DrawInfo, DrawEffects, 0.5  /* OffsetAlpha */, true  /* bIsMajor */);
		DrawViewGridLineX(OutDrawElements, DrawInfo, DrawEffects, 0.25 /* OffsetAlpha */, false /* bIsMajor */);
		DrawViewGridLineX(OutDrawElements, DrawInfo, DrawEffects, 0.0  /* OffsetAlpha */, true  /* bIsMajor */);

		// Vertical grid lines
		{
			DrawInfo.LinePoints[0].Y = DrawInfo.GetPixelTop();
			DrawInfo.LinePoints[1].Y = DrawInfo.GetPixelBottom();

			// Draw major vertical grid lines
			for (int i = 0; i < MajorGridLinesX.Num(); ++i)
			{
				const float VerticalLine = FMath::RoundToFloat(MajorGridLinesX[i]);
				const FText Label = CurveModelGridLabelsX[i];

				DrawViewGridLineY(VerticalLine, OutDrawElements, DrawInfo, DrawEffects, &Label, true /* bIsMajor */);
			}

			// Now draw the minor vertical lines which are drawn with a lighter color.
			for (float VerticalLine : MinorGridLinesX)
			{
				VerticalLine = FMath::RoundToFloat(VerticalLine);
				DrawViewGridLineY(VerticalLine, OutDrawElements, DrawInfo, DrawEffects, nullptr /* Label */, false /* bIsMajor */);
			}
		}
	}
}

void SModulationPatchEditorViewStacked::DrawViewGridLineX(FSlateWindowElementList& OutDrawElements, FModPatchViewGridDrawInfo& DrawInfo, ESlateDrawEffect DrawEffects, double OffsetAlpha, bool bIsMajor) const
{
	double ValueMin;
	double ValueMax;
	DrawInfo.GetCurveModel()->GetValueRange(ValueMin, ValueMax);

	const int32 GridLineLayerId = DrawInfo.GetBaseLayerId() + CurveViewConstants::ELayerOffset::GridLines;

	const double LowerValue = DrawInfo.GetLowerValue();
	const double PixelBottom = DrawInfo.GetPixelBottom();
	const double PixelTop = DrawInfo.GetPixelTop();

	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo FontInfo = FCoreStyle::Get().GetFontStyle("ToolTip.LargerFont");

	FLinearColor Color = bIsMajor ? DrawInfo.GetMajorGridColor() : DrawInfo.GetMinorGridColor();

	DrawInfo.LinePoints[0].Y = DrawInfo.LinePoints[1].Y = DrawInfo.ScreenSpace.ValueToScreen(LowerValue + OffsetAlpha);
	FSlateDrawElement::MakeLines(OutDrawElements, GridLineLayerId, DrawInfo.PaintGeometry, DrawInfo.LinePoints, DrawEffects, Color, false);

	FText Label = FText::AsNumber(FMath::Lerp(ValueMin, ValueMax, OffsetAlpha), &DrawInfo.LabelFormat);

	if (const FModPatchCurveEditorModel* EditorModel = static_cast<const FModPatchCurveEditorModel*>(DrawInfo.GetCurveModel()))
	{
		if (const USoundModulationPatch* Patch = EditorModel->GetPatch())
		{
			if (USoundModulationParameter* OutputParameter = Patch->PatchSettings.OutputParameter)
			{
				PatchCurveViewUtils::FormatLabel(*OutputParameter, DrawInfo.LabelFormat, Label);
			}
		}
	}

	const FVector2D LabelSize = FontMeasure->Measure(Label, FontInfo);
	double LabelY = FMath::Lerp(PixelBottom, PixelTop, OffsetAlpha);
	
	// Offset label Y position below line only if the top gridline,
	// otherwise push above
	if (FMath::IsNearlyEqual(OffsetAlpha, 1.0))
	{
		LabelY += 5.0;
	}
	else
	{
		LabelY -= 15.0;
	}

	const FPaintGeometry LabelGeometry = DrawInfo.AllottedGeometry->ToPaintGeometry(FSlateLayoutTransform(FVector2D(CurveViewConstants::CurveLabelOffsetX, LabelY)));

	FSlateDrawElement::MakeText(
		OutDrawElements,
		DrawInfo.GetBaseLayerId() + CurveViewConstants::ELayerOffset::GridLabels,
		LabelGeometry,
		Label,
		FontInfo,
		DrawEffects,
		DrawInfo.GetLabelColor()
	);
}

void SModulationPatchEditorViewStacked::DrawViewGridLineY(const float VerticalLine, FSlateWindowElementList& OutDrawElements, FModPatchViewGridDrawInfo& DrawInfo, ESlateDrawEffect DrawEffects, const FText* Label, bool bIsMajor) const
{
	const float Width = DrawInfo.AllottedGeometry->GetLocalSize().X;
	if (VerticalLine >= 0 || VerticalLine <= FMath::RoundToFloat(Width))
	{
		const int32 GridLineLayerId = DrawInfo.GetBaseLayerId() + CurveViewConstants::ELayerOffset::GridLines;

		const FLinearColor LineColor = bIsMajor ? DrawInfo.GetMajorGridColor() : DrawInfo.GetMinorGridColor();

		DrawInfo.LinePoints[0].X = DrawInfo.LinePoints[1].X = VerticalLine;
		FSlateDrawElement::MakeLines(OutDrawElements, GridLineLayerId, DrawInfo.PaintGeometry, DrawInfo.LinePoints, DrawEffects, LineColor, false);

		if (Label)
		{
			const FSlateFontInfo FontInfo = FCoreStyle::Get().GetFontStyle("ToolTip.LargerFont");

			const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			const FVector2D LabelSize = FontMeasure->Measure(*Label, FontInfo);
			const FPaintGeometry LabelGeometry = DrawInfo.AllottedGeometry->ToPaintGeometry(FSlateLayoutTransform(FVector2D(VerticalLine, DrawInfo.LinePoints[0].Y - LabelSize.Y * 1.2f)));

			FSlateDrawElement::MakeText(
				OutDrawElements,
				DrawInfo.GetBaseLayerId() + CurveViewConstants::ELayerOffset::GridLabels,
				LabelGeometry,
				*Label,
				FontInfo,
				DrawEffects,
				DrawInfo.GetLabelColor()
			);
		}
	}
}
#undef LOCTEXT_NAMESPACE