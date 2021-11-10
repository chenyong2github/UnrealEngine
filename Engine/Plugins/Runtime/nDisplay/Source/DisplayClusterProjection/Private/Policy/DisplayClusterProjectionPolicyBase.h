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

	virtual const TMap<FString, FString>& GetParameters() const override
	{
		return Parameters;
	}

	virtual bool IsConfigurationChanged(const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy) const override;

	static bool IsEditorOperationMode();

protected:
	void InitializeOriginComponent(class IDisplayClusterViewport* InViewport, const FString& OriginCopmId);
	void ReleaseOriginComponent();

private:
	// Added 'Policy' prefix to avoid "... hides class name ..." warnings in child classes
	FString ProjectionPolicyId;

	FString PolicyOriginCompId;
	TMap<FString, FString> Parameters;
	FDisplayClusterSceneComponentRef PolicyOriginComponentRef;
};
