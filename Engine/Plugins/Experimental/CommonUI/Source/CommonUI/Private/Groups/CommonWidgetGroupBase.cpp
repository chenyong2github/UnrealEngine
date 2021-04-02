// Copyright Epic Games, Inc. All Rights Reserved.

#include "Groups/CommonWidgetGroupBase.h"
#include "CommonUIPrivatePCH.h"

UCommonWidgetGroupBase::UCommonWidgetGroupBase()
{
}

void UCommonWidgetGroupBase::AddWidget(UWidget* InWidget)
{
	if (ensure(InWidget) && InWidget->IsA(GetWidgetType()))
	{
		OnWidgetAdded(InWidget);
	}
}

void UCommonWidgetGroupBase::RemoveWidget(UWidget* InWidget)
{
	if (ensure(InWidget) && InWidget->IsA(GetWidgetType()))
	{
		OnWidgetRemoved(InWidget);
	}
}

void UCommonWidgetGroupBase::RemoveAll()
{
	OnRemoveAll();
}
