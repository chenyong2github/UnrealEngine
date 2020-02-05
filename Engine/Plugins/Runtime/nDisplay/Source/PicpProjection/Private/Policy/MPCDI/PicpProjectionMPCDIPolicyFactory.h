// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Projection/IDisplayClusterProjectionPolicyFactory.h"
#include "PicpProjectionStrings.h"
#include "Policy/PicpProjectionPolicyBase.h"

class IDisplayClusterProjectionPolicyFactory;
class FPicpProjectionMPCDIViewport;


/**
 * Implements projection policy factory for the 'mpcdi' policy
 */
class FPicpProjectionMPCDIPolicyFactory
	: public IDisplayClusterProjectionPolicyFactory
{
public:
	FPicpProjectionMPCDIPolicyFactory();
	virtual ~FPicpProjectionMPCDIPolicyFactory();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterProjectionPolicyFactory
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual TSharedPtr<IDisplayClusterProjectionPolicy> Create(const FString& PolicyType, const FString& RHIName, const FString& ViewportId) override;

	TArray<TSharedPtr<FPicpProjectionPolicyBase>> GetPicpPolicy();
	TSharedPtr<FPicpProjectionPolicyBase>         GetPicpPolicyByViewport(const FString& ViewportId);

private:
	TArray<TSharedPtr<FPicpProjectionPolicyBase>> PicpPolicy;
};
