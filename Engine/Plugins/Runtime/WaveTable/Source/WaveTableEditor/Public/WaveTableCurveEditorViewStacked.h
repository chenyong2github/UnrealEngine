// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CurveModel.h"
#include "Curves/RichCurve.h"
#include "Internationalization/Text.h"
#include "RichCurveEditorModel.h"
#include "SGraphActionMenu.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Views/SCurveEditorViewStacked.h"
#include "Views/SInteractiveCurveEditorView.h"


// Forward Declarations
class UCurveFloat;


UENUM()
enum class EWaveTableCurveSource : uint8
{
	Custom,
	Expression,
	Shared,
	Unset
};


namespace WaveTable
{
	namespace Editor
	{
		class WAVETABLEEDITOR_API FWaveTableCurveModelBase : public FRichCurveEditorModelRaw
		{
		public:
			static ECurveEditorViewID ViewId;

			FWaveTableCurveModelBase(FRichCurve& InRichCurve, UObject* InOwner, EWaveTableCurveSource InSource);

			const FText& GetAxesDescriptor() const;
			const UObject* GetParentObject() const;
			EWaveTableCurveSource GetSource() const;
			void Refresh(int32 InCurveIndex);

			virtual bool IsReadOnly() const override;
			virtual FLinearColor GetColor() const override;

			int32 GetCurveIndex() const
			{
				return CurveIndex;
			}

		protected:
			virtual void RefreshCurveDescriptorText(FText& OutShortDisplayName, FText& OutInputAxisName, FText& OutOutputAxisName) = 0;

			virtual UCurveFloat* GetSharedCurve() = 0;

			virtual FColor GetCurveColor() const = 0;
			virtual bool GetPropertyEditorDisabled() const = 0;
			virtual FText GetPropertyEditorDisabledText() const = 0;

			TWeakObjectPtr<UObject> ParentObject;

		private:
			int32 CurveIndex = INDEX_NONE;
			EWaveTableCurveSource Source = EWaveTableCurveSource::Unset;

			FText InputAxisName;
			FText AxesDescriptor;
		};

		class WAVETABLEEDITOR_API SViewStacked : public SCurveEditorViewStacked
		{
		public:
			void Construct(const FArguments& InArgs, TWeakPtr<FCurveEditor> InCurveEditor);

		protected:
			virtual void PaintView(
				const FPaintArgs& Args,
				const FGeometry& AllottedGeometry,
				const FSlateRect& MyCullingRect,
				FSlateWindowElementList& OutDrawElements,
				int32 BaseLayerId, 
				const FWidgetStyle& InWidgetStyle,
				bool bParentEnabled) const override;

			virtual void DrawViewGrids(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, ESlateDrawEffect DrawEffects) const override;
			virtual void DrawLabels(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 BaseLayerId, ESlateDrawEffect DrawEffects) const override;

			virtual void FormatInputLabel(const FWaveTableCurveModelBase& EditorModel, const FNumberFormattingOptions& InLabelFormat, FText& InOutLabel) const { }
			virtual void FormatOutputLabel(const FWaveTableCurveModelBase& EditorModel, const FNumberFormattingOptions& InLabelFormat, FText& InOutLabel) const { }

		private:
			struct FGridDrawInfo
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

				int32 BaseLayerId = INDEX_NONE;

				const FCurveModel* CurveModel = nullptr;

				double LowerValue = 0.0;
				double PixelBottom = 0.0;
				double PixelTop = 0.0;

			public:
				FGridDrawInfo(const FGeometry* InAllottedGeometry, const FCurveEditorScreenSpace& InScreenSpace, FLinearColor InGridColor, int32 InBaseLayerId);

				void SetCurveModel(const FCurveModel* InCurveModel);
				const FCurveModel* GetCurveModel() const;
				void SetLowerValue(double InLowerValue);
				int32 GetBaseLayerId() const;
				FLinearColor GetLabelColor() const;
				double GetLowerValue() const;
				FLinearColor GetMajorGridColor() const;
				FLinearColor GetMinorGridColor() const;
				double GetPixelBottom() const;
				double GetPixelTop() const;
			};

			void DrawViewGridLineX(FSlateWindowElementList& OutDrawElements, FGridDrawInfo& DrawInfo, ESlateDrawEffect DrawEffect, double OffsetAlpha, bool bIsMajor) const;
			void DrawViewGridLineY(const float VerticalLine, FSlateWindowElementList& OutDrawElements, FGridDrawInfo &DrawInfo, ESlateDrawEffect DrawEffects, const FText* Label, bool bIsMajor) const;
		};
	} // namespace Editor
} // namespace WaveTable
