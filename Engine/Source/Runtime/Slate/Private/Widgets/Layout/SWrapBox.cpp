// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/Layout/SWrapBox.h"
#include "Layout/LayoutUtils.h"

SWrapBox::SWrapBox()
: Slots(this)
{
}

SWrapBox::FSlot& SWrapBox::Slot()
{
	return *( new SWrapBox::FSlot() );
}

SWrapBox::FSlot& SWrapBox::AddSlot()
{
	SWrapBox::FSlot* NewSlot = new SWrapBox::FSlot();
	Slots.Add(NewSlot);
	return *NewSlot;
}

int32 SWrapBox::RemoveSlot( const TSharedRef<SWidget>& SlotWidget )
{
	for (int32 SlotIdx = 0; SlotIdx < Slots.Num(); ++SlotIdx)
	{
		if ( SlotWidget == Slots[SlotIdx].GetWidget() )
		{
			Slots.RemoveAt(SlotIdx);
			return SlotIdx;
		}
	}

	return -1;
}

void SWrapBox::Construct( const FArguments& InArgs )
{
	PreferredSize = InArgs._PreferredSize;

	// Handle deprecation of PreferredWidth
	if (!PreferredSize.IsSet() && !PreferredSize.IsBound())
	{
		PreferredSize = InArgs._PreferredWidth;
	}

	InnerSlotPadding = InArgs._InnerSlotPadding;
	bUseAllottedSize = InArgs._UseAllottedSize || InArgs._UseAllottedWidth;
	Orientation = InArgs._Orientation;

	// Copy the children from the declaration to the widget
	for ( int32 ChildIndex=0; ChildIndex < InArgs.Slots.Num(); ++ChildIndex )
	{
		Slots.Add( InArgs.Slots[ChildIndex] );
	}
}

void SWrapBox::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	if (bUseAllottedSize)
	{
		PreferredSize = Orientation == EOrientation::Orient_Vertical ? AllottedGeometry.GetLocalSize().Y : AllottedGeometry.GetLocalSize().X;
	}
}

/*
 * Simple class for handling the somewhat complex state tracking for wrapping based on otherwise simple rules.
 * Singular static method in public interface simplifies use to a single function call by encapsulating and hiding
 * awkward inline helper object instantiation and method calls from user code. 
 */
class SWrapBox::FChildArranger
{
public:
	struct FArrangementData
	{
		FVector2D SlotOffset;
		FVector2D SlotSize;
	};

	typedef TFunctionRef<void(const FSlot& Slot, const FArrangementData& ArrangementData)> FOnSlotArranged;

	static void Arrange(const SWrapBox& WrapBox, const FOnSlotArranged& OnSlotArranged);

private:
	FChildArranger(const SWrapBox& WrapBox, const FOnSlotArranged& OnSlotArranged);
	void Arrange();
	void FinalizeLine(int32 IndexOfLastChildInCurrentLine);

	const SWrapBox& WrapBox;
	const FOnSlotArranged& OnSlotArranged;
	FVector2D Offset;
	float MaximumSizeInCurrentLine;
	int32 IndexOfFirstChildInCurrentLine;
	TMap<int32, FArrangementData> OngoingArrangementDataMap;
};


SWrapBox::FChildArranger::FChildArranger(const SWrapBox& InWrapBox, const FOnSlotArranged& InOnSlotArranged)
	: WrapBox(InWrapBox)
	, OnSlotArranged(InOnSlotArranged)
	, Offset(FVector2D::ZeroVector)
	, MaximumSizeInCurrentLine(0.0f)
	, IndexOfFirstChildInCurrentLine(INDEX_NONE)
{
	OngoingArrangementDataMap.Reserve(WrapBox.Slots.Num());
}

void SWrapBox::FChildArranger::Arrange()
{
	int32 ChildIndex;
	for (ChildIndex = 0; ChildIndex < WrapBox.Slots.Num(); ++ChildIndex)
	{
		const FSlot& Slot = WrapBox.Slots[ChildIndex];
		const TSharedRef<SWidget>& Widget = Slot.GetWidget();

		/*
		* Simple utility lambda for determining if the current child index is the first child of the current line.
		*/
		const auto& IsFirstChildInCurrentLine = [&]() -> bool
		{
			return ChildIndex == IndexOfFirstChildInCurrentLine;
		};

		// Skip collapsed widgets.
		if (Widget->GetVisibility() == EVisibility::Collapsed)
		{
			continue;
		}

		FArrangementData& ArrangementData = OngoingArrangementDataMap.Add(ChildIndex, FArrangementData());

		// If there is no first child in the current line, we must be the first child.
		if (IndexOfFirstChildInCurrentLine == INDEX_NONE)
		{
			IndexOfFirstChildInCurrentLine = ChildIndex;
		}

		/*
		* Simple utility lambda for beginning a new line with the current child, updating it's offset for the new line.
		*/
		const auto& BeginNewLine = [&]()
		{
			FinalizeLine(ChildIndex - 1);

			// Starting a new line.
			IndexOfFirstChildInCurrentLine = ChildIndex;

			// Update child's offset to new X and Y values for new line.
			ArrangementData.SlotOffset.X = Offset.X;
			ArrangementData.SlotOffset.Y = Offset.Y;
		};

		// Rule: If this child is not the first child in the line, "inner slot padding" needs to be injected left or top of it, dependently of the orientation.
		if (!IsFirstChildInCurrentLine())
		{
			Offset.Y += ((WrapBox.Orientation == EOrientation::Orient_Vertical) * WrapBox.InnerSlotPadding.Y);
			Offset.X += ((WrapBox.Orientation == EOrientation::Orient_Horizontal) * WrapBox.InnerSlotPadding.X);
		}

		const FVector2D DesiredSizeOfSlot = Slot.SlotPadding.Get().GetDesiredSize() + Widget->GetDesiredSize();

		// Populate arrangement data with default size and offset at right end of current line.
		ArrangementData.SlotOffset.X = Offset.X;
		ArrangementData.SlotOffset.Y = Offset.Y;
		ArrangementData.SlotSize.X = DesiredSizeOfSlot.X;
		ArrangementData.SlotSize.Y = DesiredSizeOfSlot.Y;

		if (WrapBox.Orientation == EOrientation::Orient_Vertical)
		{
			const float BottomBoundOfChild = ArrangementData.SlotOffset.Y + ArrangementData.SlotSize.Y;

			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			// Rule: If required due to a wrapping height under specified threshold, start a new line and allocate all of it to this child.
			if ((Slot.SlotFillLineWhenSizeLessThan.IsSet() && WrapBox.PreferredSize.Get() < Slot.SlotFillLineWhenSizeLessThan.GetValue())
				|| (!Slot.SlotFillLineWhenSizeLessThan.IsSet() && Slot.SlotFillLineWhenWidthLessThan.IsSet() && WrapBox.PreferredSize.Get() < Slot.SlotFillLineWhenWidthLessThan.GetValue()))
			{
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
				// Begin a new line if the current one isn't empty, because we demand a whole line to ourselves.
				if (!IsFirstChildInCurrentLine())
				{
					BeginNewLine();
				}

				// Fill height of rest of wrap box.
				ArrangementData.SlotSize.Y = WrapBox.PreferredSize.Get() - Offset.Y;
			}
			// Rule: If the end of a child would go beyond the width to wrap at, it should move to a new line.
			else if (BottomBoundOfChild > WrapBox.PreferredSize.Get())
			{
				// Begin a new line if the current one isn't empty, because we demand a new line.
				if (!IsFirstChildInCurrentLine())
				{
					BeginNewLine();
				}
			}

			// Update current line maximum size.
			MaximumSizeInCurrentLine = FMath::Max(MaximumSizeInCurrentLine, ArrangementData.SlotSize.X);

			// Update offset to bottom bound of child.
			Offset.Y = ArrangementData.SlotOffset.Y + ArrangementData.SlotSize.Y;
		}
		else
		{
			const float RightBoundOfChild = ArrangementData.SlotOffset.X + ArrangementData.SlotSize.X;

			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			// Rule: If required due to a wrapping width under specified threshold, start a new line and allocate all of it to this child.
			if ((Slot.SlotFillLineWhenSizeLessThan.IsSet() && WrapBox.PreferredSize.Get() < Slot.SlotFillLineWhenSizeLessThan.GetValue())
				|| (!Slot.SlotFillLineWhenSizeLessThan.IsSet() && Slot.SlotFillLineWhenWidthLessThan.IsSet() && WrapBox.PreferredSize.Get() < Slot.SlotFillLineWhenWidthLessThan.GetValue()))
			{
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
				// Begin a new line if the current one isn't empty, because we demand a whole line to ourselves.
				if (!IsFirstChildInCurrentLine())
				{
					BeginNewLine();
				}

				// Fill width of rest of wrap box.
				ArrangementData.SlotSize.X = WrapBox.PreferredSize.Get() - Offset.X;
			}
			// Rule: If the end of a child would go beyond the width to wrap at, it should move to a new line.
			else if (RightBoundOfChild > WrapBox.PreferredSize.Get())
			{
				// Begin a new line if the current one isn't empty, because we demand a new line.
				if (!IsFirstChildInCurrentLine())
				{
					BeginNewLine();
				}
			}

			// Update current line maximum size.
			MaximumSizeInCurrentLine = FMath::Max(MaximumSizeInCurrentLine, ArrangementData.SlotSize.Y);

			// Update offset to right bound of child.
			Offset.X = ArrangementData.SlotOffset.X + ArrangementData.SlotSize.X;
		}
	}

	// Attempt to finalize the final line if there are any children in it.
	if (IndexOfFirstChildInCurrentLine != INDEX_NONE)
	{
		FinalizeLine(ChildIndex - 1);
	}
}

void SWrapBox::FChildArranger::FinalizeLine(int32 IndexOfLastChildInCurrentLine)
{
	// Iterate backwards through children in this line. Iterate backwards because the last uncollapsed child may wish to fill the remaining empty space of the line.
	for (; IndexOfLastChildInCurrentLine >= IndexOfFirstChildInCurrentLine; --IndexOfLastChildInCurrentLine)
	{
		if (WrapBox.Slots[IndexOfLastChildInCurrentLine].GetWidget()->GetVisibility() != EVisibility::Collapsed)
		{
			break;
		}
	}

	// Now iterate forward so tab navigation works properly
	for (int32 ChildIndex = IndexOfFirstChildInCurrentLine; ChildIndex <= IndexOfLastChildInCurrentLine; ++ChildIndex)
	{
		const FSlot& Slot = WrapBox.Slots[ChildIndex];
		const TSharedRef<SWidget>& Widget = Slot.GetWidget();

		// Skip collapsed widgets.
		if (Widget->GetVisibility() == EVisibility::Collapsed)
		{
			continue;
		}

		FArrangementData& ArrangementData = OngoingArrangementDataMap[ChildIndex];

		// Rule: The last uncollapsed child in a line may request to fill the remaining empty space in the line.
		if (ChildIndex == IndexOfLastChildInCurrentLine && Slot.bSlotFillEmptySpace)
		{
			if (WrapBox.Orientation == EOrientation::Orient_Vertical)
			{
				ArrangementData.SlotSize.Y = WrapBox.PreferredSize.Get() - ArrangementData.SlotOffset.Y;
			}
			else
			{
				ArrangementData.SlotSize.X = WrapBox.PreferredSize.Get() - ArrangementData.SlotOffset.X;
			}
		}
		
		// All slots on this line should now match to the tallest element's height, which they can then use to do their alignment in OnSlotArranged below (eg. center within that)
		// If we left their height as is, then their slots would just be whatever their child's desired height was, and so a vertical alignment of "center" would actually 
		// leave the widget at the top of the line, since you couldn't calculate how much to offset by to actually reach the center of the "container"
		if (WrapBox.Orientation == EOrientation::Orient_Vertical)
		{
			ArrangementData.SlotSize.X = MaximumSizeInCurrentLine;
		}
		else
		{
			ArrangementData.SlotSize.Y = MaximumSizeInCurrentLine;
		}
		
		OnSlotArranged(Slot, ArrangementData);
	}

	if (WrapBox.Orientation == EOrientation::Orient_Vertical)
	{
		// Set initial state for new vertical line.
		Offset.Y = 0.0f;

		// Since this is the initial state for a new vertical line, this only happens after the first line, so the inner slot horizontal padding should always be added.
		Offset.X += MaximumSizeInCurrentLine + WrapBox.InnerSlotPadding.X;
	}
	else
	{
		// Set initial state for horizontal new line.
		Offset.X = 0.0f;

		// Since this is the initial state for a new horizontal line, this only happens after the first line, so the inner slot vertical padding should always be added.
		Offset.Y += MaximumSizeInCurrentLine + WrapBox.InnerSlotPadding.Y;
	}

	MaximumSizeInCurrentLine = 0.0f;
	IndexOfFirstChildInCurrentLine = INDEX_NONE;
}

void SWrapBox::FChildArranger::Arrange(const SWrapBox& WrapBox, const FOnSlotArranged& OnSlotArranged)
{
	FChildArranger(WrapBox, OnSlotArranged).Arrange();
}

void SWrapBox::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	FChildArranger::Arrange(*this, [&](const FSlot& Slot, const FChildArranger::FArrangementData& ArrangementData)
	{
		// Calculate offset and size in slot using alignment.
		const AlignmentArrangeResult XResult = AlignChild<Orient_Horizontal>(ArrangementData.SlotSize.X, Slot, Slot.SlotPadding.Get());
		const AlignmentArrangeResult YResult = AlignChild<Orient_Vertical>(ArrangementData.SlotSize.Y, Slot, Slot.SlotPadding.Get());

		// Note: Alignment offset is relative to slot offset.
		const FVector2D PostAlignmentOffset = ArrangementData.SlotOffset + FVector2D(XResult.Offset, YResult.Offset);
		const FVector2D PostAlignmentSize = FVector2D(XResult.Size, YResult.Size);

		ArrangedChildren.AddWidget(AllottedGeometry.MakeChild(Slot.GetWidget(), PostAlignmentOffset, PostAlignmentSize));
	});
}

void SWrapBox::ClearChildren()
{
	Slots.Empty();
}

FVector2D SWrapBox::ComputeDesiredSize( float ) const
{
	FVector2D MyDesiredSize = FVector2D::ZeroVector;

	FChildArranger::Arrange(*this, [&](const FSlot& Slot, const FChildArranger::FArrangementData& ArrangementData)
	{
		// Increase desired size to the maximum X and Y positions of any child widget.
		MyDesiredSize.X = FMath::Max(MyDesiredSize.X, ArrangementData.SlotOffset.X + ArrangementData.SlotSize.X);
		MyDesiredSize.Y = FMath::Max(MyDesiredSize.Y, ArrangementData.SlotOffset.Y + ArrangementData.SlotSize.Y);
	});

	return MyDesiredSize;
}

FChildren* SWrapBox::GetChildren()
{
	return &Slots;	
}

void SWrapBox::SetInnerSlotPadding(FVector2D InInnerSlotPadding)
{
	InnerSlotPadding = InInnerSlotPadding;
}

void SWrapBox::SetWrapWidth(const TAttribute<float>& InWrapWidth)
{
	PreferredSize = InWrapWidth;
}

void SWrapBox::SetWrapSize(const TAttribute<float>& InWrapSize)
{
	PreferredSize = InWrapSize;
}

void SWrapBox::SetUseAllottedWidth(bool bInUseAllottedWidth)
{
	bUseAllottedSize = bInUseAllottedWidth;
}

void SWrapBox::SetUseAllottedSize(bool bInUseAllottedSize)
{
	bUseAllottedSize = bInUseAllottedSize;
}

void SWrapBox::SetOrientation(EOrientation InOrientation)
{
	Orientation = InOrientation;
}