// Copyright Epic Games, Inc. All Rights Reserved.


#include "MetasoundNode.h"
#include "CoreMinimal.h"

namespace Metasound
{
	FNode::FNode(const FString& InDescription)
	:	Description(InDescription)
	{
	}

	FNode::~FNode()
	{
	}

	const FString& FNode::GetDescription() const
	{
		return Description;
	}

	const TArray<FInputDataVertexDescription>& FNode::GetInputs() const
	{
		return Inputs;
	}

	const TArray<FOutputDataVertexDescription>& FNode::GetOutputs() const
	{
		return Outputs;
	}

	void FNode::AddInputDataVertexDescription(const FInputDataVertexDescription& InDescription)
	{
		Inputs.Add(InDescription);
	}

	void FNode::RemoveInputDataVertexDescription(const FInputDataVertexDescription& InDescription)
	{
		Inputs.RemoveSingle(InDescription);
	}

	void FNode::AddOutputDataVertexDescription(const FOutputDataVertexDescription& InDescription)
	{
		Outputs.Add(InDescription);
	}

	void FNode::RemoveOutputDataVertexDescription(const FOutputDataVertexDescription& InDescription)
	{
		Outputs.RemoveSingle(InDescription);
	}
}
