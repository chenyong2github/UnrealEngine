// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if USE_USD_SDK

#include "USDIncludesStart.h"

#include "pxr/base/tf/weakBase.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/sdf/notice.h"
#include "pxr/usd/usd/notice.h"
#include "pxr/usd/usd/stage.h"

#include "USDIncludesEnd.h"

class AUsdStageActor;

class FUsdListener : public pxr::TfWeakBase
{
public:
	FUsdListener() = default;
	FUsdListener( const pxr::UsdStageRefPtr& Stage );

	virtual ~FUsdListener();

	void Register( const pxr::UsdStageRefPtr& Stage );

	DECLARE_EVENT( FUsdListener, FOnStageChanged );
	FOnStageChanged OnStageChanged;

	DECLARE_EVENT( FUsdListener, FOnStageEditTargetChanged );
	FOnStageEditTargetChanged OnStageEditTargetChanged;

	DECLARE_EVENT_TwoParams( FUsdListener, FOnPrimChanged, const FString&, bool );
	FOnPrimChanged OnPrimChanged;

	DECLARE_EVENT_OneParam( FUsdListener, FOnLayersChanged, const pxr::SdfLayerChangeListMap& );
	FOnLayersChanged OnLayersChanged;

	FThreadSafeCounter IsBlocked;

protected:
	void HandleUsdNotice( const pxr::UsdNotice::ObjectsChanged& Notice, const pxr::UsdStageWeakPtr& Sender );
	void HandleStageEditTargetChangedNotice( const pxr::UsdNotice::StageEditTargetChanged& Notice, const pxr::UsdStageWeakPtr& Sender );
	void HandleLayersChangedNotice ( const pxr::SdfNotice::LayersDidChange& Notice );

private:
	pxr::TfNotice::Key RegisteredObjectsChangedKey;
	pxr::TfNotice::Key RegisteredStageEditTargetChangedKey;
	pxr::TfNotice::Key RegisteredLayersChangedKey;
};

class FScopedBlockNotices final
{
public:
	FScopedBlockNotices( FUsdListener& InListener )
		: Listener( InListener )
	{
		Listener.IsBlocked.Increment();
	}

	~FScopedBlockNotices()
	{
		Listener.IsBlocked.Decrement();
	}

private:
	FUsdListener& Listener;
};

#endif // #if USE_USD_SDK
