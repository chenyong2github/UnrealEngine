// Copyright Epic Games, Inc. All Rights Reserved.

#include "WebAPIUtilities.h"

const FText& UWebAPIUtilities::GetResponseMessage(const FWebAPIMessageResponse& MessageResponse)
{
	return MessageResponse.GetMessage();
}
