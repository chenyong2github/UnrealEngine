// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "IPersonaPreviewScene.h"

class IEditableSkeleton;
struct FAssetData;

class SRetargetSources : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SRetargetSources )
	{}

	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs, const TSharedRef<class IEditableSkeleton>& InEditableSkeleton, const TSharedRef<class IPersonaPreviewScene>& InPreviewScene, FSimpleMulticastDelegate& InOnPostUndo);

private:

	/** The editable skeleton */
	TWeakPtr<class IEditableSkeleton> EditableSkeletonPtr;

	/** The preview scene  */
	TWeakPtr<class IPersonaPreviewScene> PreviewScenePtr;
};
