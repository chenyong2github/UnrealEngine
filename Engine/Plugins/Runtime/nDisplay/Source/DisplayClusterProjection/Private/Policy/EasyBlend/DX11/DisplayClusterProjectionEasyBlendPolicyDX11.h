// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Policy/EasyBlend/DisplayClusterProjectionEasyBlendPolicyBase.h"
#include "Policy/EasyBlend/DX11/DisplayClusterProjectionEasyBlendViewAdapterDX11.h"

#include "DisplayClusterProjectionLog.h"

/**
 * EasyBlend projection policy (DX11 implementation)
 */
class FDisplayClusterProjectionEasyBlendPolicyDX11
	: public FDisplayClusterProjectionEasyBlendPolicyBase
{
public:
	FDisplayClusterProjectionEasyBlendPolicyDX11(const FString& ViewportId)
		: FDisplayClusterProjectionEasyBlendPolicyBase(ViewportId)
	{ }

	virtual ~FDisplayClusterProjectionEasyBlendPolicyDX11()
	{ }

protected:
	virtual TSharedPtr<FDisplayClusterProjectionEasyBlendViewAdapterBase> CreateViewAdapter(const FDisplayClusterProjectionEasyBlendViewAdapterBase::FInitParams& InitParams) override
	{
		UE_LOG(LogDisplayClusterProjectionEasyBlend, Log, TEXT("Instantiating EasyBlend DX11 viewport adapter..."));
		return MakeShareable(new FDisplayClusterProjectionEasyBlendViewAdapterDX11(InitParams));
	}
};
