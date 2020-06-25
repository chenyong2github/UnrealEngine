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
}
