// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Visibility.h"
#include "Input/Reply.h"
#include "Misc/Optional.h"
#include "Templates/SharedPointer.h"
#include "Widgets/SWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class AUsdStageActor;
class FLevelCollectionModel;
class FMenuBuilder;
enum class EMapChangeType : uint8;
enum class EUsdInitialLoadSet;
struct FSlateBrush;

#if USE_USD_SDK

class SUsdStage : public SCompoundWidget
{
	SLATE_BEGIN_ARGS( SUsdStage ) {}
	SLATE_END_ARGS()

	void Construct( const FArguments& InArgs );

	virtual ~SUsdStage();

protected:
	void SetupStageActorDelegates();
	void ClearStageActorDelegates();

	TSharedRef< SWidget > MakeMainMenu();
	void FillFileMenu( FMenuBuilder& MenuBuilder );
	void FillActionsMenu( FMenuBuilder& MenuBuilder );
	void FillOptionsMenu( FMenuBuilder& MenuBuilder );
	void FillPayloadsSubMenu( FMenuBuilder& MenuBuilder );
	void FillPurposesToLoadSubMenu( FMenuBuilder& MenuBuilder );

	void OnNew();
	void OnOpen();
	void OnSave();
	void OnReloadStage();
	void OnClose();

	void OnImport();

	void OnPrimSelected( FString PrimPath );

	void OpenStage( const TCHAR* FilePath );
	void CloseStage();

	void Refresh();

	void OnStageActorLoaded( AUsdStageActor* InUsdStageActor );
	void OnStageActorPropertyChanged( UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent );

	void OnMapChanged( UWorld* World, EMapChangeType ChangeType );

protected:
	TSharedPtr< class SUsdStageInfo > UsdStageInfoWidget;
	TSharedPtr< class SUsdStageTreeView > UsdStageTreeView;
	TSharedPtr< class SUsdPrimInfo > UsdPrimInfoWidget;
	TSharedPtr< class SUsdLayersTreeView > UsdLayersTreeView;

	TWeakObjectPtr< AUsdStageActor > UsdStageActor;

	FDelegateHandle OnActorLoadedHandle;
	FDelegateHandle OnActorDestroyedHandle;
	FDelegateHandle OnStageActorPropertyChangedHandle;
	FDelegateHandle OnStageChangedHandle;
	FDelegateHandle OnStageEditTargetChangedHandle;
	FDelegateHandle OnPrimChangedHandle;

	FString SelectedPrimPath;
};

#endif // #if USE_USD_SDK
