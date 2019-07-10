// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Projection/IDisplayClusterProjectionPolicy.h"

class USceneComponent;


/**
 * Base projection policy
 */
class FDisplayClusterProjectionPolicyBase
	: public IDisplayClusterProjectionPolicy
{
public:
	FDisplayClusterProjectionPolicyBase(const FString& ViewportId);
	virtual ~FDisplayClusterProjectionPolicyBase() = 0;

protected:
	void InitializeOriginComponent(const FString& OriginCopmId);

	const FString& GetViewportId() const
	{ return PolicyViewportId; }

	const USceneComponent* const GetOriginComp() const
	{ return PolicyOriginComp; }

private:
	// Added 'Policy' prefix to avoid "... hides class name ..." warnings in child classes
	FString PolicyViewportId;
	FString PolicyOriginCompId;
	USceneComponent* PolicyOriginComp = nullptr;
};
