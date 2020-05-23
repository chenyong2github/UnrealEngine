// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundNodeInterface.h"

namespace Metasound
{
	bool operator==(const FDataVertexDescription& LHS, const FDataVertexDescription& RHS)
	{
		bool bIsEqual = /*(LHS.Source == RHS.Source) && */
			(LHS.VertexName == RHS.VertexName) &&
			(LHS.DataReferenceTypeName == RHS.DataReferenceTypeName) &&
			LHS.Tooltip.EqualTo(RHS.Tooltip);

		return bIsEqual;
	}

	bool operator!=(const FDataVertexDescription& LHS, const FDataVertexDescription& RHS)
	{
		return !(LHS == RHS);
	}
}
