// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Clients/Logging/Filter/ConcertFrontendLogFilter.h"

/** Allows only the selected messages types */
class FConcertLogFilter_MessageAction : public FConcertLogFilter
{
public:

	FConcertLogFilter_MessageAction();

	//~ Begin FConcertLogFilter Interface
	virtual bool PassesFilter(const FConcertLog& InItem) const override;
	//~ End FConcertLogFilter Interface

	void AllowAll();
	void DisallowAll();
	void ToggleAll(const TSet<FName>& ToToggle);
	
	void AllowMessageAction(FName MessageTypeName);
	void DisallowMessageAction(FName MessageTypeName);
	
	bool IsMessageActionAllowed(FName MessageTypeName) const;
	bool AreAllAllowed() const;
	uint8 GetNumSelected() const;
	
private:
	
	TSet<FName> AllowedMessageActionNames;
};

class FConcertFrontendLogFilter_MessageAction : public TConcertFrontendLogFilterAggregate<FConcertLogFilter_MessageAction>
{
public:

	FConcertFrontendLogFilter_MessageAction();

private:

	TSharedRef<SWidget> MakeSelectionMenu();
};
