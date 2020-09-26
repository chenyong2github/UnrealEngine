// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/Views/Log/IDisplayClusterConfiguratorViewLog.h"

class SDisplayClusterConfiguratorViewLog;
class FDisplayClusterConfiguratorToolkit;
class SWidget;

class FDisplayClusterConfiguratorViewLog
	: public IDisplayClusterConfiguratorViewLog
{
public:
	FDisplayClusterConfiguratorViewLog(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

	//~ Begin IDisplayClusterConfiguratorView Interface
	TSharedRef<SWidget> CreateWidget() override;
	//~ End IDisplayClusterConfiguratorView Interface

	//~ Begin IDisplayClusterConfiguratorViewLog Interface
	virtual TSharedRef<IMessageLogListing> GetMessageLogListing() const override;
	virtual TSharedRef<IMessageLogListing> CreateLogListing() override;
	virtual void Log(const FText& Message, EVerbosityLevel Verbosity = EVerbosityLevel::Log) override;
	//~ End IDisplayClusterConfiguratorViewLog Interface

private:
	TSharedPtr<SDisplayClusterConfiguratorViewLog> ViewLog;

	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;

	TSharedPtr<IMessageLogListing> MessageLogListing;

	TSharedPtr<SWidget> LogListingWidget;
};
