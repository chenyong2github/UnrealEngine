// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundVariableNodes.h"

#include "MetasoundNodeInterface.h"
#include "MetasoundVertex.h"

namespace Metasound
{
	namespace VariableNames
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


		FNodeClassName GetVariableNodeClassName(const FName& InDataTypeName)
		{
			static const FName NodeNamespace = "InitVariable";
			return FNodeClassName{NodeNamespace, InDataTypeName, ""};
		}

		FNodeClassName GetVariableMutatorNodeClassName(const FName& InDataTypeName)
		{
			static const FName NodeNamespace = "VariableMutator";
			return FNodeClassName{NodeNamespace, InDataTypeName, ""};
		}

		FNodeClassName GetVariableDeferredAccessorNodeClassName(const FName& InDataTypeName)
		{
			static const FName NodeNamespace = "VariableDeferredAccessor";
			return FNodeClassName{NodeNamespace, InDataTypeName, ""};
		}

		FNodeClassName GetVariableAccessorNodeClassName(const FName& InDataTypeName)
		{
			static const FName NodeNamespace = "VariableAccessor";
			return FNodeClassName{NodeNamespace, InDataTypeName, ""};
		}
	}
}
