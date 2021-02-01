// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Visibility.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "PersonaDelegates.h"

class UBlendSpaceBase;
class UAnimGraphNode_Base;

class SBlendSpacePreview : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBlendSpacePreview){}

	SLATE_ARGUMENT(FOnGetBlendSpaceSampleName, OnGetBlendSpaceSampleName)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UAnimGraphNode_Base* InNode);

protected:
	EVisibility GetBlendSpaceVisibility() const;
	bool GetBlendSpaceInfo(TWeakObjectPtr<const UBlendSpaceBase>& OutBlendSpace, FVector& OutPosition, FVector& OutFilteredPosition) const;

	TWeakObjectPtr<const UAnimGraphNode_Base> Node;
	TWeakObjectPtr<const UBlendSpaceBase> CachedBlendSpace;
	FVector CachedPosition;
	FVector CachedFilteredPosition;
};
