// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Render/Projection/IDisplayClusterProjectionPolicy.h"
#include "Misc/DisplayClusterObjectRef.h"

#include "DisplayClusterProjectionStrings.h"

class USceneComponent;

/**
 * Base projection policy
 */
class FDisplayClusterProjectionPolicyBase
	: public IDisplayClusterProjectionPolicy
{
public:
	FDisplayClusterProjectionPolicyBase(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy);
	virtual ~FDisplayClusterProjectionPolicyBase() = 0;

	virtual const FString& GetId() const override
	{
		return ProjectionPolicyId;
	}

	const USceneComponent* const GetOriginComp() const
	{
		return PolicyOriginComponentRef.GetOrFindSceneComponent();
	}

	USceneComponent* GetOriginComp()
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

	virtual bool IsConfigurationChanged(const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy) const override;

protected:
	void InitializeOriginComponent(class IDisplayClusterViewport* InViewport, const FString& OriginCopmId);
	void ReleaseOriginComponent(class IDisplayClusterViewport* InViewport);

private:
	// Added 'Policy' prefix to avoid "... hides class name ..." warnings in child classes
	FString ProjectionPolicyId;

	FString PolicyOriginCompId;
	TMap<FString, FString> Parameters;
	FDisplayClusterSceneComponentRef PolicyOriginComponentRef;
};
