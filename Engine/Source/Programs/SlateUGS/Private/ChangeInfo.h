// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UGSCore/EventMonitor.h"

struct FChangeInfo
{
	FDateTime Time;
	bool bHeaderRow = false;

	UGSCore::EReviewVerdict ReviewStatus = UGSCore::EReviewVerdict::Unknown;
	int Changelist = 0;
	FText Author;
	FString Description;

	// Todo: add Horde badges
};
