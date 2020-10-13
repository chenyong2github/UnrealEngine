// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"


class IDisplayClusterConfiguratorViewBase
	: public SCompoundWidget
{
public:
	virtual void ConstructChildren(const TAttribute<FMargin>& InPadding, const TSharedRef<SWidget>& InContent) = 0;
};
