// Copyright Epic Games, Inc. All Rights Reserved.

#include "Layout/Children.h"

#include "Widgets/SNullWidget.h"

FNoChildren FNoChildren::NoChildrenInstance(&SNullWidget::NullWidget.Get());

FNoChildren::FNoChildren()
	: FChildren(&SNullWidget::NullWidget.Get())
{

}

