// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertyInfoViewStyle.h"
#include "Math/Color.h"

namespace PropertyInfoViewStyle
{
	FSlateColor GetIndentBackgroundColor(int32 IndentLevel, bool IsHovered)
	{
		if (IsHovered)
		{
			return FAppStyle::Get().GetSlateColor("Colors.Header");
		}

		if (IndentLevel == 0)
		{
			return FAppStyle::Get().GetSlateColor("Colors.Panel");
		}

		int32 ColorIndex = 0;
		int32 Increment = 1;

		for (int i = 0; i < IndentLevel; ++i)
		{
			ColorIndex += Increment;

			if (ColorIndex == 0 || ColorIndex == 3)
			{
				Increment = -Increment;
			}
		}

		static const uint8 ColorOffsets[] =
		{
			2, 6, 12, 20
		};

		FColor BaseColor = FAppStyle::Get().GetSlateColor("Colors.Panel").GetSpecifiedColor().ToFColor(true);

		FColor ColorWithOffset(
		BaseColor.R + ColorOffsets[ColorIndex], 
		BaseColor.G + ColorOffsets[ColorIndex], 
		BaseColor.B + ColorOffsets[ColorIndex]);

		return FSlateColor(FLinearColor::FromSRGBColor(ColorWithOffset));
	}

	FSlateColor GetRowBackgroundColor(ITableRow* Row)
	{
		check(Row);
		return GetIndentBackgroundColor(Row->GetIndentLevel(), Row->AsWidget()->IsHovered());
	}

	void SConstrainedBox::Construct(const FArguments& InArgs)
	{
		MinWidth = InArgs._MinWidth;
		MaxWidth = InArgs._MaxWidth;

		ChildSlot
		[
			InArgs._Content.Widget
		];
	}

	FVector2D SConstrainedBox::ComputeDesiredSize(float LayoutScaleMultiplier) const
	{
		const float MinWidthVal = MinWidth.Get().Get(0.0f);
		const float MaxWidthVal = MaxWidth.Get().Get(0.0f);

		if (MinWidthVal == 0.0f && MaxWidthVal == 0.0f)
		{
			return SCompoundWidget::ComputeDesiredSize(LayoutScaleMultiplier);
		}
		else
		{
			FVector2D ChildSize = ChildSlot.GetWidget()->GetDesiredSize();

			float XVal = FMath::Max(MinWidthVal, ChildSize.X);
			if (MaxWidthVal >= MinWidthVal)
			{
				XVal = FMath::Min(MaxWidthVal, XVal);
			}

			return FVector2D(XVal, ChildSize.Y);
		}
	}

	int32 SIndent::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
							const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
							int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
	{
		constexpr int8 TabSize = 16;
		
		TSharedPtr<ITableRow> RowPtr = Row.Pin();
		if (!RowPtr.IsValid())
		{
			return LayerId;
		}

		const FSlateBrush* BackgroundBrush = FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle");
		const FSlateBrush* DropShadowBrush = FAppStyle::Get().GetBrush("DetailsView.ArrayDropShadow");

		int32 IndentLevel = RowPtr->GetIndentLevel();
		for (int32 i = 0; i < IndentLevel; ++i)
		{
			FSlateColor BackgroundColor = GetRowBackgroundColor(i);

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(FVector2D(TabSize * i, 0), FVector2D(TabSize, AllottedGeometry.GetLocalSize().Y)),
				BackgroundBrush,
				ESlateDrawEffect::None,
				BackgroundColor.GetColor(InWidgetStyle)
			);

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 1,
				AllottedGeometry.ToPaintGeometry(FVector2D(TabSize * i, 0), FVector2D(TabSize, AllottedGeometry.GetLocalSize().Y)),
				DropShadowBrush
			);
		}
			
		return LayerId + 1;
	}


	void SIndent::Construct(const FArguments& InArgs, TSharedRef<ITableRow> DetailsRow)
	{
		Row = DetailsRow;

		ChildSlot
		[
			SNew(SBox)
				.WidthOverride(this, &SIndent::GetIndentWidth)
		];
	}

	FOptionalSize SIndent::GetIndentWidth() const
	{
		int32 IndentLevel = 0;

		TSharedPtr<ITableRow> RowPtr = Row.Pin();
		if (RowPtr.IsValid())
		{
			IndentLevel = RowPtr->GetIndentLevel();
		}
		return IndentLevel * 16.0f;
	}

	FSlateColor SIndent::GetRowBackgroundColor(int32 IndentLevel) const
	{
		const bool IsHovered = Row.IsValid() && Row.Pin()->AsWidget()->IsHovered();
			
		return PropertyInfoViewStyle::GetIndentBackgroundColor(IndentLevel, IsHovered);
	}

	void SExpanderArrow::Construct(const FArguments& InArgs, TSharedRef<ITableRow> DetailsRow)
	{
		Row = DetailsRow;

		HasChildren = InArgs._HasChildren;

		ChildSlot
		[
			SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle"))
						.BorderBackgroundColor_Static(
					             PropertyInfoViewStyle::GetRowBackgroundColor,
					             Row.Pin().Get()
				             )
					[
						SNew(SBox)
							.WidthOverride(20.0f)
							.HeightOverride(16.0f)
					]
				]
				+ SOverlay::Slot()
				[
					SAssignNew(ExpanderArrow, SButton)
						.ButtonStyle(FCoreStyle::Get(), "NoBorder")
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Center)
						.ClickMethod(EButtonClickMethod::MouseDown)
						.OnClicked(this, &SExpanderArrow::OnExpanderClicked)
						.ContentPadding(FMargin(0.0f))
						.IsFocusable(false)
					[
						SNew(SImage)
							.Image(this, &SExpanderArrow::GetExpanderImage)
							.ColorAndOpacity(FSlateColor::UseForeground())
					]
				]
		];
	}

	EVisibility SExpanderArrow::GetExpanderVisibility() const
	{
		TSharedPtr<ITableRow> RowPtr = Row.Pin();
		if (!RowPtr.IsValid())
		{
			return EVisibility::Collapsed;
		}

		return RowPtr->DoesItemHaveChildren() ? EVisibility::Visible : EVisibility::Collapsed;
	}

	const FSlateBrush* SExpanderArrow::GetExpanderImage() const
	{
		TSharedPtr<ITableRow> RowPtr = Row.Pin();
		if (!RowPtr.IsValid() || !HasChildren.Get())
		{
			return FAppStyle::Get().GetBrush("NoBrush");
		}

		const bool bIsItemExpanded = RowPtr->IsItemExpanded();

		FName ResourceName;
		if (bIsItemExpanded)
		{
			if (ExpanderArrow->IsHovered())
			{
				ResourceName = TEXT("TreeArrow_Expanded_Hovered");
			}
			else
			{
				ResourceName = TEXT("TreeArrow_Expanded");
			}
		}
		else
		{
			if (ExpanderArrow->IsHovered())
			{
				ResourceName = TEXT("TreeArrow_Collapsed_Hovered");
			}
			else
			{
				ResourceName = TEXT("TreeArrow_Collapsed");
			}
		}

		return FAppStyle::Get().GetBrush(ResourceName);
	}

	FReply SExpanderArrow::OnExpanderClicked() const
	{
		TSharedPtr<ITableRow> RowPtr = Row.Pin();
		if (!RowPtr.IsValid())
		{
			return FReply::Unhandled();
		}

		// Recurse the expansion if "shift" is being pressed
		const FModifierKeysState ModKeyState = FSlateApplication::Get().GetModifierKeys();
		if (ModKeyState.IsShiftDown())
		{
			RowPtr->Private_OnExpanderArrowShiftClicked();
		}
		else
		{
			RowPtr->ToggleExpansion();
		}

		return FReply::Handled();
	}
}
