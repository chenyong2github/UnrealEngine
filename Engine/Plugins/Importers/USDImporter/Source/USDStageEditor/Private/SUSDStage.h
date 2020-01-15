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
enum class EUsdInitialLoadSet;
class FLevelCollectionModel;
class FMenuBuilder;
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

	TSharedRef< SWidget > MakeMainMenu();
	void FillFileMenu( FMenuBuilder& MenuBuilder );
	void FillActionsMenu( FMenuBuilder& MenuBuilder );

	void OnNew();
	void OnOpen();
	void OnSave();
	void OnReloadStage();

	void OnImport();

	void OnPrimSelected( FString PrimPath );
	void OnInitialLoadSetChanged( EUsdInitialLoadSet InitialLoadSet );

	void OpenStage( const TCHAR* FilePath );

	void OnStageActorLoaded( AUsdStageActor* InUsdStageActor );
	void OnStageActorPropertyChanged( UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent );

protected:
	TSharedPtr< class SUsdStageInfo > UsdStageInfoWidget;
	TSharedPtr< class SUsdStageTreeView > UsdStageTreeView;
	TSharedPtr< class SUsdPrimInfo > UsdPrimInfoWidget;
	TSharedPtr< class SUsdLayersTreeView > UsdLayersTreeView;

	TWeakObjectPtr< AUsdStageActor > UsdStageActor;

	FDelegateHandle OnActorLoadedHandle;
	FDelegateHandle OnStageActorPropertyChangedHandle;
	FDelegateHandle OnStageChangedHandle;
	FDelegateHandle OnStageEditTargetChangedHandle;
	FDelegateHandle OnPrimChangedHandle;

	FString SelectedPrimPath;
};

#endif // #if USE_USD_SDK
