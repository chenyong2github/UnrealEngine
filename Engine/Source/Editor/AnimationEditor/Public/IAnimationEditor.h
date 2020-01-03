// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WorkflowOrientedApp/WorkflowCentricApplication.h"
#include "IHasPersonaToolkit.h"

class UAnimationAsset;
class IAnimationSequenceBrowser;

class IAnimationEditor : public FWorkflowCentricApplication, public IHasPersonaToolkit
{
public:
	/** Set the animation asset of the editor. */
	virtual void SetAnimationAsset(UAnimationAsset* AnimAsset) = 0;

	/** Get the asset browser we host */
	virtual IAnimationSequenceBrowser* GetAssetBrowser() const = 0;
};
