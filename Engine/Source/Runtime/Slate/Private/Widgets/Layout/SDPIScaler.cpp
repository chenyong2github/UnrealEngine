// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/Layout/SDPIScaler.h"
#include "Layout/ArrangedChildren.h"

SDPIScaler::SDPIScaler()
	: ChildSlot(this)
{
	SetCanTick(false);
	bCanSupportFocus = false;
	bHasRelativeLayoutScale = true;
}

void SDPIScaler::Construct( const FArguments& InArgs )
{
	DPIScale = InArgs._DPIScale;

	ChildSlot
	[
		InArgs._Content.Widget
	];
}

void SDPIScaler::OnArrangeChildren( const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren ) const
{
	const EVisibility MyVisibility = this->GetVisibility();
	if ( ArrangedChildren.Accepts( MyVisibility ) )
	{
		const float MyDPIScale = DPIScale.Get();

		ArrangedChildren.AddWidget( AllottedGeometry.MakeChild(
			this->ChildSlot.GetWidget(),
			FVector2D::ZeroVector,
			AllottedGeometry.GetLocalSize() / MyDPIScale,
			MyDPIScale
		));
	}
}
	
FVector2D SDPIScaler::ComputeDesiredSize( float ) const
{
	return DPIScale.Get() * ChildSlot.GetWidget()->GetDesiredSize();
}

FChildren* SDPIScaler::GetChildren()
{
	return &ChildSlot;
}

void SDPIScaler::SetContent(TSharedRef<SWidget> InContent)
{
	ChildSlot
	[
		InContent
	];
}

void SDPIScaler::SetDPIScale(TAttribute<float> InDPIScale)
{
	if (SetAttribute(DPIScale, InDPIScale, EInvalidateWidgetReason::Layout))
	{
		InvalidatePrepass();
	}
}

float SDPIScaler::GetRelativeLayoutScale(int32 ChildIndex, float LayoutScaleMultiplier) const
{
	return DPIScale.Get();
}
