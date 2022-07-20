// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusNode.h"

#include "OptimusComponentSource.h"

#include "OptimusNode_ComponentSource.generated.h"


UCLASS(Hidden)
class UOptimusNode_ComponentSource :
	public UOptimusNode
{
	GENERATED_BODY()
public:
	void SetComponentSourceBinding(
		UOptimusComponentSourceBinding* InBinding
		);

	UOptimusComponentSourceBinding *GetComponentSourceBinding() const
	{
		return Binding;
	}

	// UOptimusNode overrides
	FName GetNodeCategory() const override;

protected:
	friend class UOptimusDeformer;

	// UOptimusNode overrides
	void ConstructNode() override;

	/** Accessor to allow the UOptimusDeformer class to hook up data interface nodes to binding nodes automatically
	 *  for backcomp.
	 */
	UOptimusNodePin* GetComponentPin() const;
	
private:
	UPROPERTY()
	TObjectPtr<UOptimusComponentSourceBinding> Binding;
};
