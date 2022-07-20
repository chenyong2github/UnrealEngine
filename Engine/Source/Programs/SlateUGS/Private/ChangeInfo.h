// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UGSCore/EventMonitor.h"

struct FChangeInfo
{
	FDateTime Time;
	bool bHeaderRow = false;

	EReviewVerdict ReviewStatus = EReviewVerdict::Unknown;
	int Changelist = 0;
	FText Author;
	FText Description;

	// Todo: add Horde badges
};
