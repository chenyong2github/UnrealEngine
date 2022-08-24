// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PackageTransmissionFilter.h"
#include "Misc/IFilter.h"

class SWidget;

namespace UE::MultiUserServer
{
	/** A filter that is intended to be displayed in the UI. Every filter has one widget displaying it. */
	class FFrontendPackageTransmissionFilter : public FPackageTransmissionFilter
	{
	public:

		/** Gets a widget that represents this filter */
		virtual TSharedRef<SWidget> GetFilterWidget() = 0;
	};

	/** Helper class to implement filters */
	template<typename TConcertLogFilter, typename TWidgetType = SWidget>
	class TFrontendPackageTransmissionFilterAggregate : public FFrontendPackageTransmissionFilter
	{
	public:

		//~ Begin FConcertLogFilter Interface
		virtual bool PassesFilter(const FPackageTransmissionEntry& InItem) const final override { return Implementation.PassesFilter(InItem); }
		//~ End FConcertLogFilter Interface
	
		//~ Begin FConcertFrontendLogFilter Interface
		virtual TSharedRef<SWidget> GetFilterWidget() final override { return ChildSlot.ToSharedRef(); }
		//~ End FConcertLogFilter Interface

	protected:

		template<typename... TArg>
		TFrontendPackageTransmissionFilterAggregate(TArg&&... Arg)
			: Implementation(Forward<TArg>(Arg)...)
		{
			Implementation.OnChanged().AddLambda([this]()
			{
				OnChanged().Broadcast();
			});
		}

		TConcertLogFilter Implementation;
		TSharedPtr<TWidgetType> ChildSlot;
	};
}


