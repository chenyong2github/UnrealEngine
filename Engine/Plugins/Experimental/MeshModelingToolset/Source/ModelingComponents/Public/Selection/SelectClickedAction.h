// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "Engine/World.h"
#include "ToolSceneQueriesUtil.h"

#pragma once

/**
 * BehaviorTarget to do world raycast selection from a click
 * Currently used to click-select reference planes in the world
 */
class FSelectClickedAction : public IClickBehaviorTarget
{
	FInputRayHit DoRayCast(const FInputDeviceRay& ClickPos, bool callbackOnHit)
	{
		FHitResult Result;

		bool bHitWorld = ToolSceneQueriesUtil::FindNearestVisibleObjectHit(
			World, Result, ClickPos.WorldRay, 
			VisibleComponentsToIgnore.Num() == 0 ? nullptr : &VisibleComponentsToIgnore,
			InvisibleComponentsToHitTest.Num() == 0 ? nullptr : &InvisibleComponentsToHitTest);

		if (callbackOnHit && bHitWorld && OnClickedPositionFunc != nullptr)
		{
			OnClickedPositionFunc(Result);
		}
		return (bHitWorld) ? FInputRayHit(Result.Distance) : FInputRayHit();
	}

public:
	UWorld* World;
	TFunction<void(const FHitResult&)> OnClickedPositionFunc = nullptr;
	TUniqueFunction<bool()> ExternalCanClickPredicate = nullptr;

	// These lists can be used to modify which components are hit tested when doing a ray cast.
	// By default, all visible components are hit tested.
	TArray<UPrimitiveComponent*> VisibleComponentsToIgnore;
	TArray<UPrimitiveComponent*> InvisibleComponentsToHitTest;

	// can alternately track shift modifier, however client must register this modifier w/ behavior
	static const int ShiftModifier = 1;
	bool bShiftModifierToggle = false;

	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override
	{
		if (ExternalCanClickPredicate && ExternalCanClickPredicate() == false)
		{
			return FInputRayHit();
		}
		return DoRayCast(ClickPos, false);
	}

	virtual void OnClicked(const FInputDeviceRay& ClickPos) override
	{
		DoRayCast(ClickPos, true);
	}


	virtual void OnUpdateModifierState(int ModifierID, bool bIsOn)
	{
		if (ModifierID == ShiftModifier)
		{
			bShiftModifierToggle = bIsOn;
		}
	}

};
