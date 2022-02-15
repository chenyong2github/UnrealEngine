// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Widgets/SCompoundWidget.h"

/**
 * SImgMediaCache manages caching for image sequences.
 */
class SImgMediaCache : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SImgMediaCache){}
	SLATE_END_ARGS()

	virtual ~SImgMediaCache();

	void Construct(const FArguments& InArgs);

private:
	/** Called when we click on the clear global cache button. */
	FReply OnClearGlobalCacheClicked();

};
