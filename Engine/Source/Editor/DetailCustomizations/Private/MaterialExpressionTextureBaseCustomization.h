// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "IDetailCustomization.h"

class IPropertyHandle;
class IDetailLayoutBuilder;
class SObjectPropertyEntryBox;
struct FAssetData;
class UMaterialExpressionTextureBase;

class FMaterialExpressionTextureBaseCustomization : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

private:
	/** IDetailCustomization interface */
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailLayout ) override;

	bool OnShouldFilterTexture(const FAssetData& AssetData);

	TWeakObjectPtr<UMaterialExpressionTextureBase> Expression;
};
