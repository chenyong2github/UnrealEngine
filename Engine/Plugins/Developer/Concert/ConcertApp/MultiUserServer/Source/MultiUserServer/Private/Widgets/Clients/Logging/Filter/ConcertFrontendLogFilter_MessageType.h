// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Clients/Logging/Filter/ConcertFrontendLogFilter.h"

/** Allows only the selected messages types */
class FConcertLogFilter_MessageType : public FConcertLogFilter
{
public:

	FConcertLogFilter_MessageType();

	//~ Begin FConcertLogFilter Interface
	virtual bool PassesFilter(const FConcertLog& InItem) const override;
	//~ End FConcertLogFilter Interface

	void AllowAll();
	void DisallowAll();
	void ToggleAll(const TSet<FName>& ToToggle);
	
	void AllowMessageType(FName MessageTypeName);
	void DisallowMessageType(FName MessageTypeName);
	
	bool IsMessageTypeAllowed(FName MessageTypeName) const;
	bool AreAllAllowed() const;
	uint8 GetNumSelected() const;
	
private:
	
	TSet<FName> AllowedMessageTypeNames;
};

class FConcertFrontendLogFilter_MessageType : public TConcertFrontendLogFilterAggregate<FConcertLogFilter_MessageType>
{
public:

	FConcertFrontendLogFilter_MessageType();

private:

	TSharedRef<SWidget> MakeSelectionMenu();
};
