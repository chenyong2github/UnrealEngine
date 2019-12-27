// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CurveModel.h"
#include "Curves/RichCurve.h"
#include "RichCurveEditorModel.h"
#include "UObject/Object.h"
#include "Views/SCurveEditorViewStacked.h"
#include "Views/SInteractiveCurveEditorView.h"

// Forward Declarations
class UCurveFloat;


enum class EModSettingsEditorCurveOutput : uint8
{
	Volume,
	Pitch,
	Highpass,
	Lowpass,
	Control,
	Count
};


enum class EModSettingsOutputEditorCurveSource : uint8
{
	Custom,
	Expression,
	Shared,
	Unset
};


struct FViewGridDrawInfo
{
	const FGeometry* AllottedGeometry;

	FPaintGeometry PaintGeometry;

	ESlateDrawEffect DrawEffects;
	FNumberFormattingOptions LabelFormat;

	TArray<FVector2D> LinePoints;

	FCurveEditorScreenSpace ScreenSpace;

private:
	FLinearColor MajorGridColor;
	FLinearColor MinorGridColor;

	int32 BaseLayerId;

	const FCurveModel* CurveModel;

	double LowerValue;
	double PixelBottom;
	double PixelTop;

public:
	FViewGridDrawInfo(const FGeometry* InAllottedGeometry, const FCurveEditorScreenSpace& InScreenSpace, FLinearColor InGridColor, int32 InBaseLayerId)
		: AllottedGeometry(InAllottedGeometry)
		, ScreenSpace(InScreenSpace)
		, BaseLayerId(InBaseLayerId)
		, CurveModel(nullptr)
		, LowerValue(0)
		, PixelBottom(0)
		, PixelTop(0)
	{
		check(AllottedGeometry);

		// Pre-allocate an array of line points to draw our vertical lines. Each major grid line
		// will overwrite the X value of both points but leave the Y value untouched so they draw from the bottom to the top.
		LinePoints.Add(FVector2D(0.f, 0.f));
		LinePoints.Add(FVector2D(0.f, 0.f));

		MajorGridColor = InGridColor;
		MinorGridColor = InGridColor.CopyWithNewOpacity(InGridColor.A * 0.5f);

		PaintGeometry = InAllottedGeometry->ToPaintGeometry();

		LabelFormat.SetMaximumFractionalDigits(6);
	}

	void SetCurveModel(const FCurveModel * InCurveModel)
	{
		CurveModel = InCurveModel;
	}

	const FCurveModel* GetCurveModel() const
	{
		return CurveModel;
	}

	void SetLowerValue(double InLowerValue)
	{
		LowerValue = InLowerValue;
		PixelBottom = ScreenSpace.ValueToScreen(InLowerValue);
		PixelTop = ScreenSpace.ValueToScreen(InLowerValue + 1.0);
	}

	int32 GetBaseLayerId() const
	{
		return BaseLayerId;
	}

	FLinearColor GetLabelColor() const
	{
		check(CurveModel);
		return GetCurveModel()->GetColor().CopyWithNewOpacity(0.7f);
	}

	double GetLowerValue() const
	{
		return LowerValue;
	}

	FLinearColor GetMajorGridColor() const
	{
		return MajorGridColor;
	}

	FLinearColor GetMinorGridColor() const
	{
		return MinorGridColor;
	}

	double GetPixelBottom() const
	{
		return PixelBottom;
	}

	double GetPixelTop() const
	{
		return PixelTop;
	}
};


class FModCurveEditorModel : public FRichCurveEditorModel
{
public:

	static ECurveEditorViewID ViewId;

	FModCurveEditorModel(FRichCurve& InRichCurve, UObject* InOwner, FName InControlName, EModSettingsOutputEditorCurveSource InSource, UCurveFloat* SharedCurve);
	FModCurveEditorModel(FRichCurve& InRichCurve, UObject* InOwner, EModSettingsEditorCurveOutput InOutput, EModSettingsOutputEditorCurveSource InSource, UCurveFloat* SharedCurve);

	virtual bool IsReadOnly() const override;

	virtual FLinearColor GetColor() const override;
	EModSettingsEditorCurveOutput GetOutput() const;
	EModSettingsOutputEditorCurveSource GetSource() const;

	void Refresh(EModSettingsEditorCurveOutput InCurveOutput, const FName* InControlName, UCurveFloat* InSharedCurve);

private:

	EModSettingsEditorCurveOutput Output;
	EModSettingsOutputEditorCurveSource Source;
};

class SModulationSettingsEditorViewStacked : public SCurveEditorViewStacked
{
public:

	void Construct(const FArguments& InArgs, TWeakPtr<FCurveEditor> InCurveEditor);

protected:
	virtual void PaintView(
		const FPaintArgs&			Args,
		const FGeometry&			AllottedGeometry,
		const FSlateRect&			MyCullingRect,
		FSlateWindowElementList&	OutDrawElements,
		int32						BaseLayerId, 
		const FWidgetStyle&			InWidgetStyle,
		bool						bParentEnabled) const override;

	virtual void DrawViewGrids(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, ESlateDrawEffect DrawEffects) const override;
	virtual void DrawLabels(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, ESlateDrawEffect DrawEffects) const override;

private:
	void DrawViewGridLineX(FSlateWindowElementList& OutDrawElements, FViewGridDrawInfo& DrawInfo, ESlateDrawEffect DrawEffect, double OffsetAlpha, bool bIsMajor) const;
	void DrawViewGridLineY(const float VerticalLine, FSlateWindowElementList& OutDrawElements, FViewGridDrawInfo &DrawInfo, ESlateDrawEffect DrawEffects, const FText* Label, bool bIsMajor) const;
};
