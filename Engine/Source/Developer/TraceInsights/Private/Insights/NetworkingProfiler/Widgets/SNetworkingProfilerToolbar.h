// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SNetworkingProfilerWindow;

class SNetworkingProfilerToolbar : public SCompoundWidget
{
public:
	/** Default constructor. */
	SNetworkingProfilerToolbar();

	/** Virtual destructor. */
	virtual ~SNetworkingProfilerToolbar();

	SLATE_BEGIN_ARGS(SNetworkingProfilerToolbar) {}
	SLATE_END_ARGS()

	/**
	 * Construct this widget
	 *
	 * @param	InArgs	The declaration data for this widget
	 */
	void Construct(const FArguments& InArgs, TSharedPtr<SNetworkingProfilerWindow> InProfilerWindow);

private:
	TSharedPtr<SNetworkingProfilerWindow> ProfilerWindow;
};
