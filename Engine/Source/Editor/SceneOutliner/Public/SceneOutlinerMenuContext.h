// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "SceneOutlinerMenuContext.generated.h"

namespace SceneOutliner
{
	class SSceneOutliner;
}

UCLASS()
class USceneOutlinerMenuContext : public UObject
{
	GENERATED_BODY()
public:

	USceneOutlinerMenuContext(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
	{
		bShowParentTree = false;
		NumSelectedItems = 0;
		NumSelectedFolders = 0;
		NumWorldsSelected = 0;
	}

	TWeakPtr<SceneOutliner::SSceneOutliner> SceneOutliner;

	bool bShowParentTree;
	int32 NumSelectedItems;
	int32 NumSelectedFolders;
	int32 NumWorldsSelected;
};
