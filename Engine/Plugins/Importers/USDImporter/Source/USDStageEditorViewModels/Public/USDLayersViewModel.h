// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Internationalization/Text.h"
#include "Templates/SharedPointer.h"

#include "USDTreeItemViewModel.h"
#include "UsdWrappers/UsdStage.h"

class USDSTAGEEDITORVIEWMODELS_API FUsdLayerModel : public TSharedFromThis< FUsdLayerModel >
{
public:
	FString DisplayName;
	bool bIsEditTarget = false;
	bool bIsMuted = false;
	bool bIsDirty = false;
};

class USDSTAGEEDITORVIEWMODELS_API FUsdLayerViewModel : public IUsdTreeViewItem
{
public:
	explicit FUsdLayerViewModel( FUsdLayerViewModel* InParentItem, const UE::FUsdStageWeak& InUsdStage, const FString& InLayerIdentifier );

	bool IsValid() const;

	TArray< TSharedRef< FUsdLayerViewModel > > GetChildren();

	void FillChildren();

	void RefreshData();

	UE::FSdfLayer GetLayer() const;
	FText GetDisplayName() const;

	bool IsLayerMuted() const;
	bool CanMuteLayer() const;
	void ToggleMuteLayer();

	bool CanEditLayer() const;
	bool EditLayer();

	void AddSubLayer( const TCHAR* SubLayerIdentifier );
	void NewSubLayer( const TCHAR* SubLayerIdentifier );
	bool RemoveSubLayer( int32 SubLayerIndex );

	bool IsLayerDirty() const;

public:
	TSharedRef< FUsdLayerModel > LayerModel;
	FUsdLayerViewModel* ParentItem;
	TArray< TSharedRef< FUsdLayerViewModel > > Children;

	UE::FUsdStageWeak UsdStage;
	FString LayerIdentifier;
};
