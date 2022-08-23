// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HitProxies.h"

// allow bone selection to edit retarget pose
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


// select chains/goals to edit chain settings
struct HIKRetargetEditorChainProxy : public HHitProxy
{
	DECLARE_HIT_PROXY();

	FName TargetChainName;
	
	HIKRetargetEditorChainProxy(const FName& InTargetChainName)
		: HHitProxy(HPP_World)
		, TargetChainName(InTargetChainName) {}

	virtual EMouseCursor::Type GetMouseCursor()
	{
		return EMouseCursor::Crosshairs;
	}

	virtual bool AlwaysAllowsTranslucentPrimitives() const override
	{
		return true;
	}
};

// select root control to edit root settings
struct HIKRetargetEditorRootProxy : public HHitProxy
{
	DECLARE_HIT_PROXY();
	
	HIKRetargetEditorRootProxy()
		: HHitProxy(HPP_World){}

	virtual EMouseCursor::Type GetMouseCursor()
	{
		return EMouseCursor::Crosshairs;
	}

	virtual bool AlwaysAllowsTranslucentPrimitives() const override
	{
		return true;
	}
};