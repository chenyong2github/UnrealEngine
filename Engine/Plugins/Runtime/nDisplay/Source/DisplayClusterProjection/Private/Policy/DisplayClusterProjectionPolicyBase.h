// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Projection/IDisplayClusterProjectionPolicy.h"
#include "Misc/DisplayClusterObjectRef.h"

class USceneComponent;


/**
 * Base projection policy
 */
class FDisplayClusterProjectionPolicyBase
	: public IDisplayClusterProjectionPolicy
{
public:
	FDisplayClusterProjectionPolicyBase(const FString& ViewportId, const TMap<FString, FString>& Parameters);
	virtual ~FDisplayClusterProjectionPolicyBase() = 0;

	const FString& GetViewportId() const
	{
		return PolicyViewportId;
	}

	const USceneComponent* const GetOriginComp() const
	{
		return PolicyOriginComponentRef.GetOrFindSceneComponent();
	}

	void SetOriginComp(USceneComponent* OriginComp)
	{
		PolicyOriginComponentRef.SetSceneComponent(OriginComp);
	}

	TMap<FString, FString>& GetParameters()
	{
		return Parameters;
	}

	const TMap<FString, FString>& GetParameters() const
	{
		return Parameters;
	}

protected:
	void InitializeOriginComponent(const FString& OriginCopmId);
	void ReleaseOriginComponent();

private:
	// Added 'Policy' prefix to avoid "... hides class name ..." warnings in child classes
	FString PolicyViewportId;
	FString PolicyOriginCompId;
	TMap<FString, FString> Parameters;
	FDisplayClusterSceneComponentRef PolicyOriginComponentRef;
};
