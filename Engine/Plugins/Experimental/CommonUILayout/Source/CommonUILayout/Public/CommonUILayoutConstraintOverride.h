// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "kiwi/kiwi.h"
#include "CommonUILayoutPanelInfo.h"
#include "CommonUILayoutConstraintOverride.generated.h"

class UWorld;
class UCommonUILayoutConstraintBase;
struct FCommonUILayoutPanelSlot;

UCLASS(Abstract, EditInlineNew, CollapseCategories)
class COMMONUILAYOUT_API UCommonUILayoutConstraintOverrideBase : public UObject
{
	GENERATED_BODY()

public:

	virtual void SetInfo(const TSoftClassPtr<UUserWidget>& Widget, const FName& UniqueID, TWeakObjectPtr<UWorld> WorldContextObject) PURE_VIRTUAL(UCommonUILayoutConstraintOverrideBase::SetInfo, );
	virtual bool TryApplyOverride(kiwi::Solver& Solver, const TMap<FCommonUILayoutPanelInfo, FCommonUILayoutPanelSlot*>& Children, const FVector2D& AllottedGeometrySize, TWeakObjectPtr<UWorld> WorldContextObject) PURE_VIRTUAL(UCommonUILayoutConstraintOverrideBase::TryApplyOverride, return false; );

};