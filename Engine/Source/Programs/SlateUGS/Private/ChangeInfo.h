// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UGSCore/EventMonitor.h"

struct FChangeInfo
{
	EReviewVerdict ReviewStatus;
	int Changelist;
	FText Time;
	FText Author;
	FText Description;
	// Todo: add Horde badges
};
