// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeKernel.h"
#include "ComputeFramework/ComputeKernelSource.h"
#include "ComputeFramework/ShaderParamTypeDefinition.h"

#if WITH_EDITOR

void UComputeKernel::PostLoad()
{
	Super::PostLoad();
	if (KernelSource != nullptr)
	{
		KernelSource->ConditionalPostLoad();
	}
}

void UComputeKernel::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	FProperty* ModifiedProperty = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue();
	if (!ModifiedProperty)
	{
		return;
	}

	bool bNotifyGraphs = false;

	FName ModifiedPropName = ModifiedProperty->GetFName();
	if (ModifiedPropName == GET_MEMBER_NAME_CHECKED(UComputeKernel, KernelSource))
	{
		if (KernelSource)
		{
			PermutationSetOverrides = KernelSource->PermutationSet;
			DefinitionsSetOverrides = KernelSource->DefinitionsSet;
		}
		else
		{
			PermutationSetOverrides = FComputeKernelPermutationSet();
			DefinitionsSetOverrides = FComputeKernelDefinitionsSet();
		}

		bNotifyGraphs = true;
	}
	else if (ModifiedPropName == GET_MEMBER_NAME_CHECKED(UComputeKernel, PermutationSetOverrides) ||
		ModifiedPropName == GET_MEMBER_NAME_CHECKED(UComputeKernel, DefinitionsSetOverrides))
	{
		bNotifyGraphs = true;
	}

	if (bNotifyGraphs)
	{
		// todo[CF]: Notify graphs to update when kernel changes.
	}
}

#endif // WITH_EDITOR
