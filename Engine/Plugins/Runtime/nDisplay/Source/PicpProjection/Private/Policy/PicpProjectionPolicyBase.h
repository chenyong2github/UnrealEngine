// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Projection/IDisplayClusterProjectionPolicy.h"
#include "IPicpProjection.h"

class USceneComponent;


/**
 * Base class for nDisplay projection policies
 */
class FPicpProjectionPolicyBase
	: public IDisplayClusterProjectionPolicy
{
public:
	FPicpProjectionPolicyBase(const FString& ViewportId);
	virtual ~FPicpProjectionPolicyBase() = 0;

	const FString& GetViewportId() const
	{
		return PolicyViewportId;
	}

protected:
	void InitializeOriginComponent(const FString& OriginCopmId);


	const USceneComponent* const GetOriginComp() const
	{
		return PolicyOriginComp;
	}

private:
	// Added 'Policy' prefix to avoid "... hides class name ..." warnings in child classes
	FString PolicyViewportId;
	FString PolicyOriginCompId;
	USceneComponent* PolicyOriginComp = nullptr;
};
