// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModulationPatchCurveEditorViewStacked.h"

#include "AudioModulationStyle.h"
#include "Curves/CurveFloat.h"
#include "CurveEditor.h"
#include "CurveModel.h"
#include "EditorStyleSet.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "IAudioModulation.h"
#include "SCurveEditorPanel.h"
#include "Sound/SoundModulationDestination.h"
#include "SoundControlBus.h"
#include "SoundModulationParameter.h"
#include "SoundModulationPatch.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "ModulationPatchEditor"

namespace PatchCurveViewUtils
{
	void FormatLabel(const USoundModulationParameter& InParameter, const FNumberFormattingOptions& InNumFormatOptions, FText& InOutLabel)
	{
		const float NormalizedValue = FCString::Atof(*InOutLabel.ToString());
		const float UnitValue = InParameter.ConvertNormalizedToUnit(NormalizedValue);
		FText UnitLabel = FText::AsNumber(UnitValue, &InNumFormatOptions);
		InOutLabel = FText::Format(LOCTEXT("ModulationPatchCurveView_UnitFormat", "{0} ({1})"), UnitLabel, InOutLabel);
	}
} // namespace PatchCurveViewUtils


FModPatchCurveEditorModel::FModPatchCurveEditorModel(FRichCurve& InRichCurve, UObject* InOwner, EModPatchOutputEditorCurveSource InSource, int32 InInputIndex)
	: FRichCurveEditorModelRaw(&InRichCurve, InOwner)
	, Patch(CastChecked<USoundModulationPatch>(InOwner))
	, InputIndex(InInputIndex)
	, Source(InSource)
{
	check(InOwner);
	Refresh(InSource, InInputIndex);
}

void FModPatchCurveEditorModel::Refresh(EModPatchOutputEditorCurveSource InSource, int32 InInputIndex)
{
	InputAxisName = LOCTEXT("ModulationCurveDisplayTitle_Normalized", "Normalized");
	FText OutputAxisName = InputAxisName;

	FSoundControlModulationInput* Input = nullptr;
	Source = EModPatchOutputEditorCurveSource::Unset;
	InputIndex = -1;

	if (Patch.IsValid() && InInputIndex >= 0 && InInputIndex < Patch->PatchSettings.Inputs.Num())
	{
		InputIndex = InInputIndex;
		Source = InSource;

		static const FText AxisNameFormat = LOCTEXT("ModulationCurveDisplayTitle_AxisNameFormat", "{0} ({1})");

		Input = &Patch->PatchSettings.Inputs[InInputIndex];
		if (const USoundControlBus* Bus = Input->GetBus())
		{
			if (USoundModulationParameter* Parameter = Bus->Parameter)
			{
				InputAxisName = FText::Format(AxisNameFormat, FText::FromString(Parameter->GetName()), Parameter->Settings.UnitDisplayName);
			}
		}

		if (USoundModulationParameter* Parameter = Patch->PatchSettings.OutputParameter)
		{
			OutputAxisName = FText::Format(AxisNameFormat, FText::FromString(Parameter->GetName()), Parameter->Settings.UnitDisplayName);
		}
	}

	AxesDescriptor = FText::Format(LOCTEXT("ModulationCurveDisplayTitle_AxesFormat", "X = {0}, Y = {1}"), InputAxisName, OutputAxisName);

	bKeyDrawEnabled = true;
	Color = UAudioModulationStyle::GetControlBusColor();
	IntentionName = TEXT("AudioControlValue");
	SupportedViews = ViewId;

	ShortDisplayName = LOCTEXT("ModulationCurveDisplayTitle_BusUnset", "Bus (Unset)");
	if (ensure(Input))
	{
		if (const USoundControlBus* Bus = Input->GetBus())
		{
			ShortDisplayName = FText::FromString(Input->GetBus()->GetName());
		}
	}

	bKeyDrawEnabled = false;

	const bool bIsBypassed = GetIsBypassed();
	if (bIsBypassed)
	{
		LongDisplayName = FText::Format(LOCTEXT("ModulationCurveDisplay_BypassedNameFormat", "{0} (Bypassed)"), ShortDisplayName);
	}
	else
	{
		UEnum* Enum = StaticEnum<EModPatchOutputEditorCurveSource>();
		check(Enum);

		FString EnumValueName = Enum->GetValueAsString(Source);
		int32 DelimIndex = -1;
		EnumValueName.FindLastChar(':', DelimIndex);
		if (DelimIndex > 0 && DelimIndex < EnumValueName.Len() - 1)
		{
			EnumValueName.RightChopInline(DelimIndex + 1);
		}

		FText CurveSourceText = FText::FromString(EnumValueName);

		if (Source == EModPatchOutputEditorCurveSource::Custom)
		{
			bKeyDrawEnabled = true;
		}
		else if (Source == EModPatchOutputEditorCurveSource::Shared)
		{
			check(Input);
			UCurveFloat* SharedCurve = Input->Transform.CurveShared;
			check(SharedCurve);
			CurveSourceText = FText::Format(LOCTEXT("ModulationCurveDisplayTitle_SharedNameFormat", "Shared, {0}"), FText::FromString(SharedCurve->GetName()));
			bKeyDrawEnabled = true;
		}

		LongDisplayName = FText::Format(LOCTEXT("ModulationCurveDisplayTitle_NameFormat", "{0}: {1} ({2})"), FText::AsNumber(InInputIndex), ShortDisplayName, CurveSourceText);
	}
}

FLinearColor FModPatchCurveEditorModel::GetColor() const
{
	return !GetIsBypassed() && (Source == EModPatchOutputEditorCurveSource::Custom)
		? Color
		: Color.Desaturate(0.35f);
}

const FText& FModPatchCurveEditorModel::GetAxesDescriptor() const
{
	return AxesDescriptor;
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

USoundModulationParameter* FModPatchCurveEditorModel::GetPatchInputParameter() const
{
	if (Patch.IsValid() && InputIndex >= 0 && InputIndex < Patch->PatchSettings.Inputs.Num())
	{
		if (const USoundControlBus* Bus = Patch->PatchSettings.Inputs[InputIndex].GetBus())
		{
			return Bus->Parameter;
		}
	}

	return nullptr;
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

	StackedHeight = 300.0f;
	StackedPadding = 60.0f;
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
	const FCurveEditorScreenSpace ViewSpace = GetViewSpace();

	// Draw the curve labels for each view
	for (auto It = CurveInfoByID.CreateConstIterator(); It; ++It)
	{
		FCurveModel* Curve = CurveEditor->FindCurve(It.Key());
		if (!ensureAlways(Curve))
		{
			continue;
		}

		const int32  CurveIndexFromBottom = CurveInfoByID.Num() - It->Value.CurveIndex - 1;
		const double PaddingToBottomOfView = (CurveIndexFromBottom + 1) * ValueSpacePadding;

		const float PixelBottom = ViewSpace.ValueToScreen(CurveIndexFromBottom + PaddingToBottomOfView);
		const float PixelTop = ViewSpace.ValueToScreen(CurveIndexFromBottom + PaddingToBottomOfView + 1.0);

		if (!FSlateRect::DoRectanglesIntersect(MyCullingRect, TransformRect(AllottedGeometry.GetAccumulatedLayoutTransform(), FSlateRect(0, PixelTop, LocalSize.X, PixelBottom))))
		{
			continue;
		}

		const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

		// Render label
		FText Label = Curve->GetLongDisplayName();

		static const float LabelOffsetX = 15.0f;
		static const float LabelOffsetY = 35.0f;
		FVector2D LabelPosition(LabelOffsetX, PixelTop - LabelOffsetY);
		FPaintGeometry LabelGeometry = AllottedGeometry.ToPaintGeometry(FSlateLayoutTransform(LabelPosition));
		const FVector2D LabelSize = FontMeasure->Measure(Label, FontInfo);

		FSlateDrawElement::MakeText(OutDrawElements, LabelLayerId + 1, LabelGeometry, Label, FontInfo, DrawEffects, Curve->GetColor());

		// Render axes descriptor
		FText Descriptor = static_cast<FModPatchCurveEditorModel*>(Curve)->GetAxesDescriptor();

		const FVector2D DescriptorSize = FontMeasure->Measure(Descriptor, FontInfo);

		static const float LabelBufferX = 20.0f; // Keeps label and axes descriptor visually separated
		static const float GutterBufferX = 20.0f; // Accounts for potential scroll bar
		const float ViewWidth = ViewSpace.GetPhysicalWidth();
		const float FloatingDescriptorX = ViewWidth - DescriptorSize.X;
		LabelPosition = FVector2D(FMath::Max(LabelSize.X + LabelBufferX, FloatingDescriptorX - GutterBufferX), PixelTop - LabelOffsetY);
		LabelGeometry = AllottedGeometry.ToPaintGeometry(FSlateLayoutTransform(LabelPosition));

		FSlateDrawElement::MakeText(OutDrawElements, LabelLayerId + 1, LabelGeometry, Descriptor, FontInfo, DrawEffects, Curve->GetColor());
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
			if (USoundModulationParameter* InputParameter = EditorModel->GetPatchInputParameter())
			{
				for (int32 i = 0; i < CurveModelGridLabelsX.Num(); ++i)
				{
					FText& Label = CurveModelGridLabelsX[i];
					PatchCurveViewUtils::FormatLabel(*InputParameter, DrawInfo.LabelFormat, Label);
				}
			}
		}

		const int32 Index = CurveInfoByID.Num() - It->Value.CurveIndex - 1;
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