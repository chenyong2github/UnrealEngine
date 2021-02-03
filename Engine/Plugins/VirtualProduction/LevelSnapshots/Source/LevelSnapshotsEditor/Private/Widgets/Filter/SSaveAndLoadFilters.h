// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class UFilterLoader;

/* Creates buttons for save & load of filters. */
class SSaveAndLoadFilters : public SCompoundWidget
{
public:

	~SSaveAndLoadFilters(); 
	
	SLATE_BEGIN_ARGS(SSaveAndLoadFilters)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UFilterLoader* InFilterLoader);

private:
	
	void RebuildButtons();

	TWeakObjectPtr<UFilterLoader> FilterLoader;
	/* Subscription to FilterLoader */
	FDelegateHandle OnEditedAssetChangedHandle;
};
