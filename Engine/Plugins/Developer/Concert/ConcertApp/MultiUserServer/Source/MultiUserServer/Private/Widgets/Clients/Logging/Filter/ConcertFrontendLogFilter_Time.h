// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertLogFilterTypes.h"

#include "Widgets/Util/Filter/ConcertFilter.h"
#include "Widgets/Util/Filter/ConcertFrontendFilter.h"

namespace UE::MultiUserServer
{
	enum class ETimeFilter
	{
		/** Logs after the indicated time are allowed */
		AllowAfter,
		/** Logs before the indicated time are allowed */
		AllowBefore
	};

	/** Filters based on whether a log happened before or after a certain time */
	class FConcertLogFilter_Time : public FConcertLogFilter
	{
	public:

		FConcertLogFilter_Time(ETimeFilter FilterMode);

		void ResetToInfiniteTime();

		//~ Begin FConcertLogFilter Interface
		virtual bool PassesFilter(const FConcertLogEntry& InItem) const override;
		//~ End FConcertLogFilter Interfac

		ETimeFilter GetFilterMode() const { return FilterMode; }
		FDateTime GetTime() const { return Time; }

		void SetFilterMode(ETimeFilter InFilterMode);
		void SetTime(const FDateTime& InTime);
	
	private:

		ETimeFilter FilterMode;
		FDateTime Time;

		FDateTime MakeResetTime() const;
	};

	class FConcertFrontendLogFilter_Time : public TConcertFrontendFilterAggregate<FConcertLogFilter_Time, const FConcertLogEntry&>
	{
		using Super = TConcertFrontendFilterAggregate<FConcertLogFilter_Time, const FConcertLogEntry&>;
	public:
	
		FConcertFrontendLogFilter_Time(ETimeFilter TimeFilter);

	private:

		TSharedRef<SWidget> CreateDatePicker();
	};
}

