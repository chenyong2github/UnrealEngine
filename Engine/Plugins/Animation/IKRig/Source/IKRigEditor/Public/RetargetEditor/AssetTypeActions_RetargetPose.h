// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/IToolkitHost.h"
#include "AssetTypeActions_Base.h"
#include "EditorAnimUtils.h"

class URetargetPose;

class FAssetTypeActions_RetargetPose : public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions Implementation
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_RetargetPose", "Retarget Pose"); }
	virtual FColor GetTypeColor() const override { return FColor(0, 40, 255); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Animation; }
	const TArray<FText>& GetSubMenus() const override;
	virtual UThumbnailInfo* GetThumbnailInfo(UObject* Asset) const override;
	virtual void GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section) override;
};
