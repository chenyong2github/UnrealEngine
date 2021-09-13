// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundVariableNodes.h"
#include "CoreMinimal.h"

namespace Metasound
{
	namespace VariableNodeVertexNames
	{
		const FVertexName& GetInputDataName()
		{
			static const FVertexName Name = TEXT("Value");
			return Name;
		}

		const FVertexName& GetOutputDataName()
		{
			static const FVertexName Name = TEXT("Value");
			return Name;
		}

		const FVertexName& GetInputVariableName()
		{
			static const FVertexName Name = TEXT("Variable");
			return Name;
		}

		const FVertexName& GetOutputVariableName()
		{
			static const FVertexName Name = TEXT("Variable");
			return Name;
		}
	}
}
