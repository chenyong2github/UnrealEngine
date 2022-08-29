// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/IFilter.h"
#include "Templates/UnrealTemplate.h"

namespace UE::MultiUserServer
{
	/** Base filter */
	template<typename TFilterType>
	class TConcertFilter
		:
		public IFilter<TFilterType>,
		// We do not need to copy filters so let's avoid it by accident; some constructors pass this pointer to callbacks which become stale upon copying
		public FNoncopyable
	{
	public:

		//~ Begin IFilter Interface
		DECLARE_DERIVED_EVENT( TConcertFilter<TFilterType>, IFilter<TFilterType>::FChangedEvent, FChangedEvent );
		virtual FChangedEvent& OnChanged() override { return ChangedEvent; }
		//~ End IFilter Interface

	protected:
	
		void BroadcastChangedEvent() const { ChangedEvent.Broadcast(); }

	private:
	
		FChangedEvent ChangedEvent;
	};
}

