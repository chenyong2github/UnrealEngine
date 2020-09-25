// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class SWidget;


class IDisplayClusterConfiguratorView
	: public TSharedFromThis<IDisplayClusterConfiguratorView>
{
public:
	virtual ~IDisplayClusterConfiguratorView()
	{ }

public:
	virtual TSharedRef<SWidget> CreateWidget() = 0;
};
