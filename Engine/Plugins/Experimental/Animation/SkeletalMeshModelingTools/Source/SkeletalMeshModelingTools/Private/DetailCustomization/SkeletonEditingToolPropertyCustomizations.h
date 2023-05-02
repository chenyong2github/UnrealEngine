// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

#include "SAdvancedTransformInputBox.h"
#include "SkeletalMesh/SkeletonEditingTool.h"

class IDetailLayoutBuilder;

class FSkeletonEditingPropertiesDetailCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	
private:

	void CustomizeValueGet(SAdvancedTransformInputBox<FTransform>::FArguments& InOutArgs);
	void CustomizeValueSet(SAdvancedTransformInputBox<FTransform>::FArguments& InOutArgs);
	void CustomizeClipboard(SAdvancedTransformInputBox<FTransform>::FArguments& InOutArgs);
	
	TWeakObjectPtr<USkeletonEditingTool> Tool;
	TUniquePtr<SkeletonEditingTool::FRefSkeletonChange> ActiveChange;
	TBitArray<> RelativeArray;
};

