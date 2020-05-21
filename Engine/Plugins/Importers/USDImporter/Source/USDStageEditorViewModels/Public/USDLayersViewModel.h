// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Internationalization/Text.h"
#include "Templates/SharedPointer.h"

#include "USDTreeItemViewModel.h"
#include "UsdWrappers/UsdStage.h"

namespace UE
{
	class FSdfLayer;
}

class USDSTAGEEDITORVIEWMODELS_API FUsdLayerModel : public TSharedFromThis< FUsdLayerModel >
{
public:
	FText GetDisplayName() const { return DisplayName; }

	FText DisplayName;
	bool bIsEditTarget = false;
	bool bIsMuted = false;
};

class USDSTAGEEDITORVIEWMODELS_API FUsdLayerViewModel : public IUsdTreeViewItem
{
public:
	explicit FUsdLayerViewModel( FUsdLayerViewModel* InParentItem, const UE::FUsdStage& InUsdStage, const FString& InLayerIdentifier );

	bool IsValid() const;

	TArray< TSharedRef< FUsdLayerViewModel > > GetChildren();

	void FillChildren();

	void RefreshData();

	UE::FSdfLayer GetLayer() const;

	bool CanMuteLayer() const;
	void ToggleMuteLayer();

	bool CanEditLayer() const;
	bool EditLayer();

	void AddSubLayer( const TCHAR* SubLayerIdentifier );
	void NewSubLayer( const TCHAR* SubLayerIdentifier );
	bool RemoveSubLayer( int32 SubLayerIndex );

public:
	TSharedRef< FUsdLayerModel > LayerModel;
	FUsdLayerViewModel* ParentItem;
	TArray< TSharedRef< FUsdLayerViewModel > > Children;

	UE::FUsdStage UsdStage;
	FString LayerIdentifier;
};
