// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SDMXInputInfoSelecter;
class SDMXInputInfo;

/** Widget to hold tab widgets */
class SDMXInputConsole
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXInputConsole)
	{}

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	const TSharedRef<SDMXInputInfoSelecter> GetInputInfoSelecter() const { return InputInfoSelecter.ToSharedRef(); }

	const TSharedRef<SDMXInputInfo> GetInputInfo() const { return InputInfo.ToSharedRef(); }

private:
	void OnUniverseSelectionChanged(const FName& InProtocol);

private:

	TSharedPtr<SDMXInputInfoSelecter> InputInfoSelecter;

	TSharedPtr<SDMXInputInfo> InputInfo;
};
