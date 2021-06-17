// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimBlueprintDetails.h"
#include "Animation/AnimBlueprint.h"
#include "DetailLayoutBuilder.h"

#define LOCTEXT_NAMESPACE "AnimBlueprintDetails"

void FAnimBlueprintDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	UAnimBlueprint* Asset = CastChecked<UAnimBlueprint>(Objects[0].Get());

	if(Asset->bIsTemplate)
	{
		TSharedRef<IPropertyHandle> TargetSkeletonHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UAnimBlueprint, TargetSkeleton));
		TargetSkeletonHandle->MarkHiddenByCustomization();
	}
}

#undef LOCTEXT_NAMESPACE