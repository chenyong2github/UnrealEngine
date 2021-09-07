// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundVariableNodes.h"
#include "CoreMinimal.h"

namespace Metasound
{
	namespace VariableNodeVertexNames
	{
		const FString& GetInputDataName()
		{
			static const FString Name = TEXT("Value");
			return Name;
		}

		const FString& GetOutputDataName()
		{
			static const FString Name = TEXT("Value");
			return Name;
		}

		const FString& GetInputVariableName()
		{
			static const FString Name = TEXT("Variable");
			return Name;
		}

		const FString& GetOutputVariableName()
		{
			static const FString Name = TEXT("Variable");
			return Name;
		}
	}
}
