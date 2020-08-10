// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundNodeInterface.h"

namespace Metasound
{
	bool operator==(const FDataVertex& LHS, const FDataVertex& RHS)
	{
		bool bIsEqual = /*(LHS.Source == RHS.Source) && */
			(LHS.VertexName == RHS.VertexName) &&
			(LHS.DataReferenceTypeName == RHS.DataReferenceTypeName) &&
			LHS.Description.EqualTo(RHS.Description);

		return bIsEqual;
	}

	bool operator!=(const FDataVertex& LHS, const FDataVertex& RHS)
	{
		return !(LHS == RHS);
	}

	bool operator==(const FOutputDataSource& InLeft, const FOutputDataSource& InRight)
	{
		return (InLeft.Node == InRight.Node) && (InLeft.Vertex == InRight.Vertex);
	}

	/** Check if two FInputDataDestinations are equal. */
	bool operator==(const FInputDataDestination& InLeft, const FInputDataDestination& InRight)
	{
		return (InLeft.Node == InRight.Node) && (InLeft.Vertex == InRight.Vertex);
	}

	/** Check if two FDataEdges are equal. */
	bool operator==(const FDataEdge& InLeft, const FDataEdge& InRight)
	{
		return (InLeft.From == InRight.From) && (InLeft.To == InRight.To);
	}
}
