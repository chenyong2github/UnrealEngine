// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/BaseAssetToolkit.h"

class UDisplayClusterConfiguratorEditorData;
class IDisplayClusterConfiguratorViewDetails;
class IDisplayClusterConfiguratorView;
class IDisplayClusterConfiguratorViewLog;
class IDisplayClusterConfiguratorViewOutputMapping;
class IDisplayClusterConfiguratorViewTree;
class IDisplayClusterConfiguratorViewViewport;
class UDisplayClusterConfigurationData;
class IMessageLogListing;
class UAssetEditor;
class UObject;

class IDisplayClusterConfiguratorToolkit
	: public FBaseAssetToolkit
{
public:
	DECLARE_MULTICAST_DELEGATE(FOnConfigReloaded);
	DECLARE_MULTICAST_DELEGATE(FOnObjectSelected);
	DECLARE_MULTICAST_DELEGATE(FOnInvalidateViews);
	DECLARE_MULTICAST_DELEGATE(FOnClearViewportSelection);

	using FOnConfigReloadedDelegate = FOnConfigReloaded::FDelegate;
	using FOnObjectSelectedDelegate = FOnObjectSelected::FDelegate;
	using FOnInvalidateViewsDelegate = FOnInvalidateViews::FDelegate;
	using FOnClearViewportSelectionDelegate = FOnClearViewportSelection::FDelegate;

public:
	IDisplayClusterConfiguratorToolkit(UAssetEditor* InAssetEditor)
		: FBaseAssetToolkit(InAssetEditor)
	{}

	virtual ~IDisplayClusterConfiguratorToolkit() {}

	virtual UDisplayClusterConfiguratorEditorData* GetEditorData() const = 0;

	/** Registers a delegate to be called when the selected items have changed */
	virtual FDelegateHandle RegisterOnConfigReloaded(const FOnConfigReloadedDelegate& Delegate) = 0;

	/** Unregisters a delegate to be called when the selected items have changed */
	virtual void UnregisterOnConfigReloaded(FDelegateHandle DelegateHandle) = 0;

	virtual FDelegateHandle RegisterOnObjectSelected(const FOnObjectSelectedDelegate& Delegate) = 0;

	virtual void UnregisterOnObjectSelected(FDelegateHandle DelegateHandle) = 0;

	virtual FDelegateHandle RegisterOnInvalidateViews(const FOnInvalidateViewsDelegate& Delegate) = 0;

	virtual void UnregisterOnInvalidateViews(FDelegateHandle DelegateHandle) = 0;

	virtual TSharedRef<IDisplayClusterConfiguratorViewOutputMapping> GetViewOutputMapping() const = 0;

	virtual TSharedRef<IDisplayClusterConfiguratorViewTree> GetViewCluster() const = 0;

	virtual TSharedRef<IDisplayClusterConfiguratorViewTree> GetViewScene() const = 0;
	virtual TSharedRef<IDisplayClusterConfiguratorViewTree> GetViewInput() const = 0;

	virtual TSharedRef<IDisplayClusterConfiguratorViewViewport> GetViewViewport() const = 0;

	virtual TSharedRef<IDisplayClusterConfiguratorViewLog> GetViewLog() const = 0;

	virtual TSharedRef<IDisplayClusterConfiguratorViewDetails> GetViewDetails() const = 0;

	virtual TSharedRef<IDisplayClusterConfiguratorView> GetViewGeneral() const = 0;

	virtual const TArray<UObject*>& GetSelectedObjects() const = 0;

	virtual void SelectObjects(TArray<UObject*>& InSelectedObjects) = 0;

	virtual void InvalidateViews() = 0;

	virtual void ClearViewportSelection() = 0;

	virtual UDisplayClusterConfigurationData* GetConfig() const = 0;
};
