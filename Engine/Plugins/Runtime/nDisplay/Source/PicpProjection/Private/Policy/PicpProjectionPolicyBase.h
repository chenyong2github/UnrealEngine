// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Projection/IDisplayClusterProjectionPolicy.h"
#include "IPicpProjection.h"
#include "Misc/DisplayClusterObjectRef.h"

/**
 * Base class for nDisplay projection policies
 */
class FPicpProjectionPolicyBase
	: public IDisplayClusterProjectionPolicy
{
public:
	FPicpProjectionPolicyBase(const FString& ViewportId, const TMap<FString, FString>& InParameters);
	virtual ~FPicpProjectionPolicyBase() = 0;

public:
	const FString& GetViewportId() const
	{
		return PolicyViewportId;
	}

	const TMap<FString, FString>& GetParameters() const
	{
		return Parameters;
	}

protected:
	void InitializeOriginComponent(const FString& OriginCopmId);
	void ReleaseOriginComponent();


	const USceneComponent* const GetOriginComp() const
	{
		return PolicyOriginComponentRef.GetOrFindSceneComponent();
	}

private:
	// Added 'Policy' prefix to avoid "... hides class name ..." warnings in child classes
	FString PolicyViewportId;
	FString PolicyOriginCompId;
	TMap<FString, FString> Parameters;
	FDisplayClusterSceneComponentRef PolicyOriginComponentRef;
};
