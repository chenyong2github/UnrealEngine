// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ContainersFwd.h"
#include "Containers/Map.h"
#include "Delegates/Delegate.h"

class FUsdListenerImpl;

namespace UE
{
	class FUsdStage;
}

/**
 * Registers to Usd Notices and emits events when the Usd Stage has changed
 */
class UNREALUSDWRAPPER_API FUsdListener
{
public:
	FUsdListener();
	FUsdListener( const UE::FUsdStage& Stage );

	FUsdListener( const FUsdListener& Other ) = delete;
	FUsdListener( FUsdListener&& Other ) = delete;

	virtual ~FUsdListener();

	FUsdListener& operator=( const FUsdListener& Other ) = delete;
	FUsdListener& operator=( FUsdListener&& Other ) = delete;

	void Register( const UE::FUsdStage& Stage );

	// Increment/decrement the block counter
	void Block();
	void Unblock();
	bool IsBlocked() const;

	DECLARE_EVENT( FUsdListener, FOnStageEditTargetChanged );
	FOnStageEditTargetChanged& GetOnStageEditTargetChanged();

	using FPrimsChangedList = TMap< FString, bool >;
	DECLARE_EVENT_OneParam( FUsdListener, FOnPrimsChanged, const FPrimsChangedList& );
	FOnPrimsChanged& GetOnPrimsChanged();

	using FStageChangedFields = TArray< FString >;
	DECLARE_EVENT_OneParam( FUsdListener, FOnStageInfoChanged, const FStageChangedFields& );
	FOnStageInfoChanged& GetOnStageInfoChanged();

	DECLARE_EVENT_OneParam( FUsdListener, FOnLayersChanged, const TArray< FString >& );
	FOnLayersChanged& GetOnLayersChanged();

private:
	TUniquePtr< FUsdListenerImpl > Impl;
};

class UNREALUSDWRAPPER_API FScopedBlockNotices final
{
public:
	explicit FScopedBlockNotices( FUsdListener& InListener );
	~FScopedBlockNotices();

	FScopedBlockNotices() = delete;
	FScopedBlockNotices( const FScopedBlockNotices& ) = delete;
	FScopedBlockNotices( FScopedBlockNotices&& ) = delete;
	FScopedBlockNotices& operator=( const FScopedBlockNotices& ) = delete;
	FScopedBlockNotices& operator=( FScopedBlockNotices&& ) = delete;

private:
	FUsdListener& Listener;
};
