// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/IFilter.h"

namespace UE::MultiUserServer
{
	struct FPackageTransmissionEntry;

	/** Base filter */
	class FPackageTransmissionFilter
		:
		public IFilter<const FPackageTransmissionEntry&>,
		public TSharedFromThis<FPackageTransmissionFilter>,
		// We do not need to copy filters so let's avoid it by accident; some constructors pass this pointer to callbacks which become stale upon copying
		public FNoncopyable
	{
	public:

		//~ Begin IFilter Interface
		DECLARE_DERIVED_EVENT( FFrontendFilter, IFilter<const FPackageTransmissionEntry&>::FChangedEvent, FChangedEvent );
		virtual FChangedEvent& OnChanged() override { return ChangedEvent; }
		//~ End IFilter Interface

		protected:
	
		void BroadcastChangedEvent() const { ChangedEvent.Broadcast(); }

	private:
	
		FChangedEvent ChangedEvent;
	};
}


