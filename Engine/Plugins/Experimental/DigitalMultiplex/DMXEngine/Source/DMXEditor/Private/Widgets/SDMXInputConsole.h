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
	DECLARE_DELEGATE(FOnResetUISequanceID);

	SLATE_BEGIN_ARGS(SDMXInputConsole)
	{}

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	const TSharedRef<SDMXInputInfoSelecter> GetInputInfoSelecter() const { return InputInfoSelecter.ToSharedRef(); }

	const TSharedRef<SDMXInputInfo> GetInputInfo() const { return InputInfo.ToSharedRef(); }

private:
	/** Propagates changes from input universe box */
	void OnUniverseSelectionChanged(const FName& InProtocol);

	/** Propagates changes from listen for popup menu */
	void OnListenForChanged(const FName& InListenFor);

	/** Propagates Clear universe button */
	void OnClearUniverses();

	/** Propagates Clear channels view button */
	void OnClearChannelsView();

private:
	TSharedPtr<SDMXInputInfoSelecter> InputInfoSelecter;

	TSharedPtr<SDMXInputInfo> InputInfo;
};
