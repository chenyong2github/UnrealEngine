// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class IDisplayClusterProjectionPolicy;


/**
 * nDisplay projection policy factory interface
 */
class IDisplayClusterProjectionPolicyFactory
{
public:
	virtual ~IDisplayClusterProjectionPolicyFactory() = 0
	{ }

public:
	/**
	* Creates a projection policy instance
	*
	* @param PolicyType - Projection policy type, same as specified on registration (useful if the same factory is registered for multiple projection types)
	* @param RHIName    - RHI name that the sync policy is requested for
	* @param ViewportId - ID of a viewport the policy is requested for
	*
	* @return - Projection policy instance
	*/
	UE_DEPRECATED(4.26, "Please use 'Create'  with an extended argument list")
	virtual TSharedPtr<IDisplayClusterProjectionPolicy> Create(const FString& PolicyType, const FString& RHIName, const FString& ViewportId)
	{ return nullptr; }

	/**
	* Creates a projection policy instance
	*
	* @param PolicyType - Projection policy type, same as specified on registration (useful if the same factory is registered for multiple projection types)
	* @param RHIName    - RHI name that the sync policy is requested for
	* @param ViewportId - ID of a viewport the policy is requested for
	*
	* @return - Projection policy instance
	*/
	virtual TSharedPtr<IDisplayClusterProjectionPolicy> Create(const FString& PolicyType, const FString& RHIName, const FString& ViewportId, const TMap<FString, FString>& Parameters) = 0;
};
