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

	const FInputDataVertexCollection& FNode::GetInputDataVertices() const
	{
		return Inputs;
	}

	const FOutputDataVertexCollection& FNode::GetOutputDataVertices() const
	{
		return Outputs;
	}

	void FNode::AddInputDataVertex(const FInputDataVertex& InVertex)
	{
		Inputs.Add(MakeDataVertexKey(InVertex), InVertex);
	}

	void FNode::RemoveInputDataVertex(const FInputDataVertex& InVertex)
	{
		Inputs.Remove(MakeDataVertexKey(InVertex));
	}

	void FNode::AddOutputDataVertex(const FOutputDataVertex& InVertex)
	{
		Outputs.Add(MakeDataVertexKey(InVertex), InVertex);
	}

	void FNode::RemoveOutputDataVertex(const FOutputDataVertex& InVertex)
	{
		Outputs.Remove(MakeDataVertexKey(InVertex));
	}
}
