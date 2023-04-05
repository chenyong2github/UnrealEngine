// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundInputNode.h"

#include "MetasoundDataReference.h"
#include "MetasoundVertex.h"
#include "MetasoundVertexData.h"
#include "UObject/NameTypes.h"

namespace Metasound
{
	namespace MetasoundInputNodePrivate
	{
		FDataReferenceCollection FInputOperatorBase::GetInputs() const
		{
			// This is slated to be deprecated and removed.
			checkNoEntry();
			return {};
		}

		FDataReferenceCollection FInputOperatorBase::GetOutputs() const
		{
			// This is slated to be deprecated and removed.
			checkNoEntry();
			return {};
		}

		FNonExecutableInputOperatorBase::FNonExecutableInputOperatorBase(const FVertexName& InVertexName, FAnyDataReference&& InDataRef)
		: VertexName(InVertexName)
		, DataRef(MoveTemp(InDataRef))
		{
		}

		void FNonExecutableInputOperatorBase::Bind(FVertexInterfaceData& InOutVertexData) const
		{
			BindInputs(InOutVertexData.GetInputs());
			BindOutputs(InOutVertexData.GetOutputs());
		}

		void FNonExecutableInputOperatorBase::BindInputs(FInputVertexInterfaceData& InOutVertexData) const
		{
			InOutVertexData.BindVertex(VertexName, DataRef);
		}

		void FNonExecutableInputOperatorBase::BindOutputs(FOutputVertexInterfaceData& InOutVertexData) const
		{
			InOutVertexData.BindVertex(VertexName, DataRef);
		}

		IOperator::FExecuteFunction FNonExecutableInputOperatorBase::GetExecuteFunction()
		{
			return nullptr;
		}

		IOperator::FPostExecuteFunction FNonExecutableInputOperatorBase::GetPostExecuteFunction()
		{
			return nullptr;
		}

		IOperator::FResetFunction FNonExecutableInputOperatorBase::GetResetFunction() 
		{
			return nullptr;
		}
	}
}
