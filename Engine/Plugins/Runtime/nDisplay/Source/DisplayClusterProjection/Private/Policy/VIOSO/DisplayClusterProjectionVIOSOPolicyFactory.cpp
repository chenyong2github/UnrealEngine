// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/VIOSO/DisplayClusterProjectionVIOSOPolicyFactory.h"

#include "DisplayClusterProjectionStrings.h"
#include "DisplayClusterProjectionLog.h"

#include "DisplayClusterConfigurationTypes.h"

#if PLATFORM_WINDOWS
#include "Policy/VIOSO/Windows/DisplayClusterProjectionVIOSOPolicy.h"
#endif


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicyFactory
//////////////////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IDisplayClusterProjectionPolicy> FDisplayClusterProjectionVIOSOPolicyFactory::Create(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy)
{
	check(InConfigurationProjectionPolicy != nullptr);

#if PLATFORM_WINDOWS
	UE_LOG(LogDisplayClusterProjectionVIOSO, Log, TEXT("Instantiating projection policy <%s> id='%s'"), *InConfigurationProjectionPolicy->Type, *ProjectionPolicyId);
	return  MakeShared<FDisplayClusterProjectionVIOSOPolicy>(ProjectionPolicyId, InConfigurationProjectionPolicy);
#endif

	return nullptr;
}
