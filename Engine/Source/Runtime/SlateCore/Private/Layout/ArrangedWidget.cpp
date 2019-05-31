// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Layout/ArrangedWidget.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWidget.h"

// Use function to initialize because SNullWidget::NullWidget is not statically initialized yet
const FArrangedWidget& FArrangedWidget::GetNullWidget()
{
	static FArrangedWidget NullArrangedWidget(SNullWidget::NullWidget, FGeometry());
	checkSlow(&SNullWidget::NullWidget.Get() != nullptr);
	return NullArrangedWidget;
}

FString FArrangedWidget::ToString( ) const
{
	return FString::Printf(TEXT("%s @ %s"), *Widget->ToString(), *Geometry.ToString());
}


FWidgetAndPointer::FWidgetAndPointer()
: FArrangedWidget(FArrangedWidget::GetNullWidget())
, PointerPosition(TSharedPtr<FVirtualPointerPosition>())
{}

FWidgetAndPointer::FWidgetAndPointer( const FArrangedWidget& InWidget, const TSharedPtr<FVirtualPointerPosition>& InPosition )
: FArrangedWidget(InWidget)
, PointerPosition(InPosition)
{}
