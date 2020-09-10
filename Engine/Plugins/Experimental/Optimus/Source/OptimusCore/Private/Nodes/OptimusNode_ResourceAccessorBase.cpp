// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNode_ResourceAccessorBase.h"

#include "OptimusCoreModule.h"
#include "OptimusResourceDescription.h"


void UOptimusNode_ResourceAccessorBase::SetResourceDescription(UOptimusResourceDescription* InResourceDesc)
{
	if (!ensure(InResourceDesc))
	{
		return;
	}

	if (!EnumHasAnyFlags(InResourceDesc->DataType->UsageFlags, EOptimusDataTypeUsageFlags::Resource))
	{
		UE_LOG(LogOptimusCore, Error, TEXT("Data type '%s' is not usable in a resource"),
		    *InResourceDesc->DataType->TypeName.ToString());
		return;
	}
	ResourceDesc = InResourceDesc;
}


UOptimusResourceDescription* UOptimusNode_ResourceAccessorBase::GetResourceDescription() const
{
	return ResourceDesc.Get();
}
