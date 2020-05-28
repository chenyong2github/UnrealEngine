// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Tools/BaseAssetToolkit.h"

class FExampleAssetToolkit : public FBaseAssetToolkit
{
public:
	FExampleAssetToolkit(UAssetEditor* InOwningAssetEditor);
	virtual ~FExampleAssetToolkit();
private:
	FDelegateHandle WindowOpenedDelegateHandle{};
	bool bWindowHasOpened{false};
};
