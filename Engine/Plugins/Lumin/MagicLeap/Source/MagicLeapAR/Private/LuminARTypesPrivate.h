// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MagicLeapHandle.h"

struct LuminArTrackable
{
	LuminArTrackable()
		: Handle(MagicLeap::INVALID_FGUID)
	{}

	// Use FGuid since a LuminArTrackable can either be an MLHandle or an MLCFUID (currently only for PCFs)
	FGuid Handle;
};

struct LuminArAnchor : public LuminArTrackable
{
	LuminArAnchor()
		: LuminArTrackable()
		, ParentTrackable(MagicLeap::INVALID_FGUID)
	{}

	LuminArAnchor(const FGuid& InParentTrackable)
		: LuminArTrackable()
		, ParentTrackable(InParentTrackable)
	{}

	void Detach()
	{
		ParentTrackable = MagicLeap::INVALID_FGUID;
	}

	// Use FGuid since a LuminArTrackable can either be an MLHandle or an MLCFUID (currently only for PCFs)
	FGuid ParentTrackable;
};
