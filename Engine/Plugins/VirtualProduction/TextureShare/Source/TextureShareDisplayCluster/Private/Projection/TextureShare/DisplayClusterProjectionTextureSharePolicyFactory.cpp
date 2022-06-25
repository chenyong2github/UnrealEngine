// Copyright Epic Games, Inc. All Rights Reserved.

#include "Projection/TextureShare/DisplayClusterProjectionTextureSharePolicyFactory.h"
#include "Projection/TextureShare/DisplayClusterProjectionTextureSharePolicy.h"

#include "Module/TextureShareDisplayClusterLog.h"

#include "DisplayClusterConfigurationTypes.h"

//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterProjectionTextureSharePolicyFactory
//////////////////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe> FDisplayClusterProjectionTextureSharePolicyFactory::Create(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy)
{
	check(InConfigurationProjectionPolicy != nullptr);

	UE_LOG(LogTextureShareDisplayClusterProjection, Verbose, TEXT("Instantiating projection policy <%s> id='%s'"), *InConfigurationProjectionPolicy->Type, *ProjectionPolicyId);

	return  MakeShared<FDisplayClusterProjectionTextureSharePolicy, ESPMode::ThreadSafe>(ProjectionPolicyId, InConfigurationProjectionPolicy);
}
