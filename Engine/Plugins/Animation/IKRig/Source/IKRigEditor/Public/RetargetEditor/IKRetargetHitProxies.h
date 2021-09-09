// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HitProxies.h"

// allow limb selection to edit retarget pose
struct HIKRetargetEditorBoneProxy : public HHitProxy
{
	DECLARE_HIT_PROXY();

	FName BoneName;
	
	HIKRetargetEditorBoneProxy(const FName& InBoneName)
		: HHitProxy(HPP_World)
		, BoneName(InBoneName) {}

	virtual EMouseCursor::Type GetMouseCursor()
	{
		return EMouseCursor::Crosshairs;
	}

	virtual bool AlwaysAllowsTranslucentPrimitives() const override
	{
		return true;
	}
};
