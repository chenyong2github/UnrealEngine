// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/WidgetSwitcher.h"
#include "CommonActivatableWidget.h"
#include "Slate/SCommonAnimatedSwitcher.h"
#include "CommonAnimatedSwitcher.h"
#include "Components/Widget.h"

#include "CommonActivatableWidgetSwitcher.generated.h"

/**
 * An animated switcher that knows about CommonActivatableWidgets. It can also hold other Widgets.
 */
UCLASS()
class COMMONUI_API UCommonActivatableWidgetSwitcher : public UCommonAnimatedSwitcher
{
	GENERATED_BODY()

protected:
	virtual void HandleOutgoingWidget() override;
	virtual void HandleSlateActiveIndexChanged(int32 ActiveIndex) override;
};