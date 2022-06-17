// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertFrontendLogFilter_BaseSetSelection.h"
#include "Widgets/Clients/Logging/ConcertLogEntry.h"
#include "Widgets/Clients/Logging/Filter/ConcertFrontendLogFilter.h"
#include "Widgets/Clients/Logging/Util/MessageActionUtils.h"

namespace UE::MultiUserServer::Filters
{
	/** Allows only the selected messages types */
	class FConcertLogFilter_MessageAction : public TConcertLogFilter_BaseSetSelection<FConcertLogFilter_MessageAction, FName>
	{
	public:

		static TSet<FName> GetAllOptions()
		{
			return MessageActionUtils::GetAllMessageActionNames();
		}
		
		static FString GetOptionDisplayString(const FName& Item)
		{
			return MessageActionUtils::GetActionDisplayString(Item);
		}

		//~ Begin FConcertLogFilter Interface
		virtual bool PassesFilter(const FConcertLogEntry& InItem) const override
		{
			return IsItemAllowed(MessageActionUtils::ConvertActionToName(InItem.Log.MessageAction));
		}
		//~ End FConcertLogFilter Interface
	};

	class FConcertFrontendLogFilter_MessageAction : public TConcertFrontendLogFilter_BaseSetSelection<FConcertLogFilter_MessageAction>
	{
	public:

		FConcertFrontendLogFilter_MessageAction()
			: TConcertFrontendLogFilter_BaseSetSelection<FConcertLogFilter_MessageAction>(NSLOCTEXT("UnrealMultiUserUI.Filter.MessageAction", "Name", "Actions"))
		{}
	};
}

