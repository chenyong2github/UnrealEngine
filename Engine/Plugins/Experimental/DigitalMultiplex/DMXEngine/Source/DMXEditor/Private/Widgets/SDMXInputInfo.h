// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

/** DMX Container for Universe and Channel Monitor widget */
class SDMXInputInfo
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXInputInfo)
	{}
	SLATE_ARGUMENT(TWeakPtr<class SDMXInputInfoSelecter>, InfoSelecter)
	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	/** Switch to  Channel monitor mode */
	void ChangeToLookForAddresses();

	/** Switch to Universe Monitor mode*/
	void ChangeToLookForUniverses();
	
	/** Clear universe values store by UI */
	void ClearUniverses();

	/** Clear channel values stored by UI */
	void ClearChannelsView();

	/** Propgates changes to universe value for Channel monitor*/
	void UniverseSelectionChanged();

	const TSharedPtr<class SDMXInputInfoChannelsView>& GetChannelsView() const
	{
		return ChannelsView;
	}

	const TSharedPtr<class SDMXInputInfoUniverseMonitor>& GetUniversesView() const
	{
		return UniversesView;
	}

protected:
	TWeakPtr<class SDMXInputInfoSelecter> WeakInfoSelecter;
	
	TSharedPtr<class SDMXInputInfoChannelsView> ChannelsView;

	TSharedPtr<class SDMXInputInfoUniverseMonitor> UniversesView;

};

