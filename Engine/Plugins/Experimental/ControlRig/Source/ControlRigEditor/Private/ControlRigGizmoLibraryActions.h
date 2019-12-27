// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"
#include "ControlRigGizmoLibrary.h"

class FMenuBuilder;

class FControlRigGizmoLibraryActions : public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions Implementation
	virtual bool HasActions(const TArray<UObject*>& InObjects) const override
	{
		return true;
	}
	virtual void GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder) override;
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_ControlRigGizmoLibrary", "Gizmo Library"); }
	virtual FColor GetTypeColor() const override { return FColor(100,100,255); }
	virtual UClass* GetSupportedClass() const override { return UControlRigGizmoLibrary::StaticClass(); }
	virtual bool CanFilter() override { return true; }
	virtual uint32 GetCategories() override { return EAssetTypeCategories::Animation; }
};
