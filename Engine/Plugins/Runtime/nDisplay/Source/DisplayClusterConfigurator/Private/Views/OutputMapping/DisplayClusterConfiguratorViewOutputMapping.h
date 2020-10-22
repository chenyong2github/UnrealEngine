// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/Views/OutputMapping/IDisplayClusterConfiguratorViewOutputMapping.h"

#include "UObject/StrongObjectPtr.h"

class FDisplayClusterConfiguratorToolkit;
class SDisplayClusterConfiguratorGraphEditor;
class SDisplayClusterConfiguratorViewOutputMapping;
class UDisplayClusterConfiguratorGraph;

class FDisplayClusterConfiguratorViewOutputMapping
	: public IDisplayClusterConfiguratorViewOutputMapping
{
public:
	FDisplayClusterConfiguratorViewOutputMapping(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

	//~ Begin IDisplayClusterConfiguratorView Interface
	virtual TSharedRef<SWidget> CreateWidget() override;

	virtual void SetEnabled(bool bInEnabled) override;
	//~ End IDisplayClusterConfiguratorView Interface

	//~ Begin IDisplayClusterConfiguratorView Interface
	virtual bool IsRulerVisible() const override;

	virtual FDelegateHandle RegisterOnShowWindowInfo(const FOnShowWindowInfoDelegate& Delegate) override;

	virtual void UnregisterOnShowWindowInfo(FDelegateHandle DelegateHandle) override;

	virtual FDelegateHandle RegisterOnShowWindowCornerImage(const FOnShowWindowCornerImageDelegate& Delegate) override;

	virtual void UnregisterOnShowWindowCornerImage(FDelegateHandle DelegateHandle) override;

	virtual FDelegateHandle RegisterOnShowOutsideViewports(const FOnShowOutsideViewportsDelegate& Delegate) override;

	virtual void UnregisterOnShowOutsideViewports(FDelegateHandle DelegateHandle) override;

	virtual FOnOutputMappingBuilt& GetOnOutputMappingBuiltDelegate() override
	{ return OnOutputMappingBuilt; }

	virtual FDelegateHandle RegisterOnOutputMappingBuilt(const FOnOutputMappingBuiltDelegate& Delegate) override;

	virtual void UnregisterOnOutputMappingBuilt(FDelegateHandle DelegateHandle) override;

	virtual bool IsShowOutsideViewports() const override
	{ return bShowOutsideViewports; }

	virtual bool IsShowWindowInfo() const override
	{ return bShowWindowInfo; }

	virtual bool IsShowWindowCornerImage() const override
	{ return bShowWindowCornerImage; }

	virtual void SetViewportPreviewTexture(const FString& NodeId, const FString& ViewportId, UTexture* InTexture) override;
	//~ End IDisplayClusterConfiguratorView Interface

	void ToggleShowWindowInfo();

	void ToggleShowWindowCornerImage();

	void ToggleShowOutsideViewports();

private:
	TSharedPtr<SDisplayClusterConfiguratorViewOutputMapping> ViewOutputMapping;

	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;

	TSharedPtr<SDisplayClusterConfiguratorGraphEditor> GraphEditor;

	TStrongObjectPtr<UDisplayClusterConfiguratorGraph> GraphObj;

	bool bShowWindowInfo;

	bool bShowWindowCornerImage;

	bool bShowOutsideViewports;

	FOnShowWindowInfo OnShowWindowInfo;

	FOnShowWindowCornerImage OnShowWindowCornerImage;

	FOnShowOutsideViewports OnShowOutsideViewports;

	FOnOutputMappingBuilt OnOutputMappingBuilt;
};
