// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CurveModel.h"
#include "Curves/RichCurve.h"
#include "RichCurveEditorModel.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Views/SCurveEditorViewStacked.h"
#include "Views/SInteractiveCurveEditorView.h"

#include "ModulationPatchCurveEditorViewStacked.generated.h"

// Forward Declarations
class UCurveFloat;
class USoundModulationParameter;
class USoundModulationPatch;
struct FSoundControlModulationInput;


UENUM()
enum class EModPatchOutputEditorCurveSource : uint8
{
	Custom,
	Expression,
	Shared,
	Unset
};


struct FModPatchViewGridDrawInfo
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
	FModPatchViewGridDrawInfo(const FGeometry* InAllottedGeometry, const FCurveEditorScreenSpace& InScreenSpace, FLinearColor InGridColor, int32 InBaseLayerId)
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

		LabelFormat.SetMaximumFractionalDigits(2);
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


class FModPatchCurveEditorModel : public FRichCurveEditorModelRaw
{
public:
	static ECurveEditorViewID ViewId;

	FModPatchCurveEditorModel(FRichCurve& InRichCurve, UObject* InOwner, EModPatchOutputEditorCurveSource InSource, int32 InInputIndex);

	const FText& GetAxesDescriptor() const;
	bool GetIsBypassed() const;
	USoundModulationParameter* GetPatchInputParameter() const;
	const USoundModulationPatch* GetPatch() const;
	EModPatchOutputEditorCurveSource GetSource() const;
	void Refresh(EModPatchOutputEditorCurveSource InSource, int32 InInputIndex);

	virtual bool IsReadOnly() const override;
	virtual FLinearColor GetColor() const override;

private:

	TWeakObjectPtr<USoundModulationPatch> Patch;
	int32 InputIndex = -1;
	EModPatchOutputEditorCurveSource Source = EModPatchOutputEditorCurveSource::Unset;

	FText InputAxisName;
	FText AxesDescriptor;
};

class SModulationPatchEditorViewStacked : public SCurveEditorViewStacked
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
	void DrawViewGridLineX(FSlateWindowElementList& OutDrawElements, FModPatchViewGridDrawInfo& DrawInfo, ESlateDrawEffect DrawEffect, double OffsetAlpha, bool bIsMajor) const;
	void DrawViewGridLineY(const float VerticalLine, FSlateWindowElementList& OutDrawElements, FModPatchViewGridDrawInfo &DrawInfo, ESlateDrawEffect DrawEffects, const FText* Label, bool bIsMajor) const;
};
