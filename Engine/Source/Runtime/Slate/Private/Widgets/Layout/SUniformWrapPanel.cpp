// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/Layout/SUniformWrapPanel.h"
#include "Layout/LayoutUtils.h"

SUniformWrapPanel::SUniformWrapPanel()
: Children(this)
, HAlign(EHorizontalAlignment::HAlign_Left)
, EvenRowDistribution(false)
{
}

void SUniformWrapPanel::Construct( const FArguments& InArgs )
{
	SlotPadding = InArgs._SlotPadding;
	NumColumns = 0;
	NumRows = 0;
	MinDesiredSlotWidth = InArgs._MinDesiredSlotWidth.Get();
	MinDesiredSlotHeight = InArgs._MinDesiredSlotHeight.Get();
	EvenRowDistribution =  InArgs._EvenRowDistribution.Get();
	HAlign = InArgs._HAlign.Get();

	Children.Reserve( InArgs.Slots.Num() );
	for (int32 ChildIndex=0; ChildIndex < InArgs.Slots.Num(); ChildIndex++)
	{
		FSlot* ChildSlot = InArgs.Slots[ChildIndex];
		Children.Add( ChildSlot );
	}
}

void SUniformWrapPanel::OnArrangeChildren( const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren ) const
{

	if ( Children.Num() > 0)
	{

		FVector2D CellSize = ComputeUniformCellSize();
		NumColumns = FMath::Max(1, FMath::Min(NumVisibleChildren, FMath::FloorToInt( AllottedGeometry.GetLocalSize().X / CellSize.X )));
		NumRows = FMath::CeilToInt ( (float) NumVisibleChildren / (float) NumColumns );

		// If we have to have N rows, try to distribute the items across the rows evenly
		int32 AdjNumColumns = EvenRowDistribution.Get() ? FMath::CeilToInt( (float) NumVisibleChildren / (float) NumRows ) : NumColumns;

		float LeftSlop = 0.0f;

		switch (HAlign.Get())
		{
			case HAlign_Fill:
			{
				// CellSize = FVector2D(AllottedGeometry.GetLocalSize().X / NumColumns, AllottedGeometry.GetLocalSize().Y / NumRows);
				CellSize = FVector2D (AllottedGeometry.GetLocalSize().X / AdjNumColumns, CellSize.Y);
				break;
			}
			case HAlign_Center:
			{

				LeftSlop = FMath::FloorToFloat((AllottedGeometry.GetLocalSize().X - (CellSize.X * AdjNumColumns)) / 2.0f);
				break;
			}
			case HAlign_Right:
			{
				LeftSlop = 	FMath::FloorToFloat(AllottedGeometry.GetLocalSize().X - (CellSize.X * AdjNumColumns));
				break;
			}
		};


		const FMargin& CurrentSlotPadding(SlotPadding.Get());
		int32 VisibleChildIndex = 0;
		for ( int32 ChildIndex=0; ChildIndex < Children.Num(); ++ChildIndex )
		{
			const FSlot& Child = Children[ChildIndex];
			const EVisibility ChildVisibility = Child.GetWidget()->GetVisibility();
			if ( ArrangedChildren.Accepts(ChildVisibility)  && !Child.GetWidget()->GetDesiredSize().IsZero() )
			{

				// Do the standard arrangement of elements within a slot
				// Takes care of alignment and padding.
				AlignmentArrangeResult XAxisResult = AlignChild<Orient_Horizontal>(CellSize.X, Child, CurrentSlotPadding);
				AlignmentArrangeResult YAxisResult = AlignChild<Orient_Vertical>(CellSize.Y, Child, CurrentSlotPadding);

				int32 col = VisibleChildIndex % AdjNumColumns;
				int32 row = VisibleChildIndex / AdjNumColumns;

				if (row == (NumRows - 1))
				{
					float NumLastRowColumns = NumVisibleChildren % AdjNumColumns != 0 ? NumVisibleChildren % AdjNumColumns : AdjNumColumns;
					if (HAlign.Get() == HAlign_Right)
					{
						LeftSlop = FMath::FloorToFloat(AllottedGeometry.GetLocalSize().X - (CellSize.X * NumLastRowColumns));
					}
					else if (HAlign.Get() == HAlign_Center)
					{
						LeftSlop = FMath::FloorToFloat((AllottedGeometry.GetLocalSize().X - (CellSize.X * NumLastRowColumns)) / 2.0f);
					}
				}

				ArrangedChildren.AddWidget(ChildVisibility,
					AllottedGeometry.MakeChild(Child.GetWidget(),
					FVector2D(CellSize.X*col + XAxisResult.Offset + LeftSlop, CellSize.Y*row + YAxisResult.Offset),
					FVector2D(XAxisResult.Size, YAxisResult.Size)
					));

				VisibleChildIndex++;
			}
		}
	}
}

FVector2D SUniformWrapPanel::ComputeUniformCellSize() const
{
	FVector2D MaxChildDesiredSize = FVector2D::ZeroVector;
	const FVector2D SlotPaddingDesiredSize = SlotPadding.Get().GetDesiredSize();
	
	const float CachedMinDesiredSlotWidth = MinDesiredSlotWidth.Get();
	const float CachedMinDesiredSlotHeight = MinDesiredSlotHeight.Get();
	
	NumColumns = 0;
	NumRows = 0;

	NumVisibleChildren = 0;
	for ( int32 ChildIndex=0; ChildIndex < Children.Num(); ++ChildIndex )
	{
		const FSlot& Child = Children[ ChildIndex ];
		if (Child.GetWidget()->GetVisibility() != EVisibility::Collapsed)
		{
			FVector2D ChildDesiredSize = Child.GetWidget()->GetDesiredSize();
			if (!ChildDesiredSize.IsZero())
			{
				NumVisibleChildren++;
				ChildDesiredSize += SlotPaddingDesiredSize;

				ChildDesiredSize.X = FMath::Max( ChildDesiredSize.X, CachedMinDesiredSlotWidth);
				ChildDesiredSize.Y = FMath::Max( ChildDesiredSize.Y, CachedMinDesiredSlotHeight);

				MaxChildDesiredSize.X = FMath::Max( MaxChildDesiredSize.X, ChildDesiredSize.X );
				MaxChildDesiredSize.Y = FMath::Max( MaxChildDesiredSize.Y, ChildDesiredSize.Y );
			}

		}
	}

	return MaxChildDesiredSize;
}

FVector2D SUniformWrapPanel::ComputeDesiredSize( float ) const
{
	FVector2D MaxChildDesiredSize = ComputeUniformCellSize();

	if (NumVisibleChildren > 0)
	{
		// Try to use the currente geometry .  If the preferred width or geometry isn't avaialble
		// then try to make a square.
		const FVector2D& LocalSize = GetTickSpaceGeometry().GetLocalSize();
		if (!LocalSize.IsZero()) 
		{
			NumColumns = FMath::FloorToInt( LocalSize.X / MaxChildDesiredSize.X );
		}
		else 
		{
			NumColumns = FMath::CeilToInt(FMath::Sqrt((float)NumVisibleChildren));
		}

		NumRows = FMath::CeilToInt ( (float) NumVisibleChildren / (float) NumColumns );

		return FVector2D( NumColumns*MaxChildDesiredSize.X, NumRows*MaxChildDesiredSize.Y );
	}

	return FVector2D::ZeroVector;
}

FChildren* SUniformWrapPanel::GetChildren()
{
	return &Children;
}

void SUniformWrapPanel::SetSlotPadding(TAttribute<FMargin> InSlotPadding)
{
	SlotPadding = InSlotPadding;
}

void SUniformWrapPanel::SetMinDesiredSlotWidth(TAttribute<float> InMinDesiredSlotWidth)
{
	MinDesiredSlotWidth = InMinDesiredSlotWidth;
}

void SUniformWrapPanel::SetMinDesiredSlotHeight(TAttribute<float> InMinDesiredSlotHeight)
{
	MinDesiredSlotHeight = InMinDesiredSlotHeight;
}

void SUniformWrapPanel::SetHorizontalAlignment(TAttribute<EHorizontalAlignment> InHAlignment)
{
	HAlign = InHAlignment;	
}

void SUniformWrapPanel::SetEvenRowDistribution(TAttribute<bool> InEvenRowDistribution)
{
	EvenRowDistribution = InEvenRowDistribution;
}

SUniformWrapPanel::FSlot& SUniformWrapPanel::AddSlot()
{
	FSlot& NewSlot = *(new FSlot());

	Children.Add( &NewSlot );

	return NewSlot;
}

bool SUniformWrapPanel::RemoveSlot( const TSharedRef<SWidget>& SlotWidget )
{
	for (int32 SlotIdx = 0; SlotIdx < Children.Num(); ++SlotIdx)
	{
		if ( SlotWidget == Children[SlotIdx].GetWidget() )
		{
			Children.RemoveAt(SlotIdx);
			return true;
		}
	}
	
	return false;
}

void SUniformWrapPanel::ClearChildren()
{
	NumColumns = 0;
	NumRows = 0;
	Children.Empty();
}
