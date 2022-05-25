// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

struct HordeBuildRowInfo
{
	bool bBuildStatus;
	// Data for CODE/CONTENT
	FText Changelist;
	FText Time; // Todo: maybe use a Time class of some kind
	FText Author;
	FText Description;
	// Data for EDITOR
	// Data for PLATFORMS
	// Data for CIS
	FText Status;
};