// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundNodeInterface.h"

#define LOCTEXT_NAMESPACE "MetasoundOutputNode"

namespace Metasound
{
	template<typename DataType>
	class TOutputNode : public INode
	{
			class FOutputOperator : public IOperator
			{
				public:
					using FDataReadReference = TDataReadReference<DataType>;

					FOutputOperator(const FString& InDataReferenceName, FDataReadReference InDataReference)
					{
						Outputs.AddDataReadReference<DataType>(InDataReferenceName, InDataReference);
					}

					virtual ~FOutputOperator() {}

					virtual const FDataReferenceCollection& GetInputs() const override
					{
						return Inputs;
					}

					virtual const FDataReferenceCollection& GetOutputs() const override
					{
						return Outputs;
					}

					virtual FExecuteFunction GetExecuteFunction() override
					{
						return nullptr;
					}

				private:
					FDataReferenceCollection Inputs;
					FDataReferenceCollection Outputs;
			};

			class FOutputOperatorFactory : public IOperatorFactory
			{
				public:
					FOutputOperatorFactory(const FString& InDataReferenceName)
					:	DataReferenceName(InDataReferenceName)
					{
					}

					virtual TUniquePtr<IOperator> CreateOperator(const INode& InNode, const FOperatorSettings& InOperatorSettings, const FDataReferenceCollection& InInputDataReferences, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors) override
					{
						if (!InInputDataReferences.ContainsDataReadReference<DataType>(DataReferenceName))
						{
							// TODO: Add build error.
							return TUniquePtr<IOperator>(nullptr);
						}

						return MakeUnique<FOutputOperator>(DataReferenceName, InInputDataReferences.GetDataReadReference<DataType>(DataReferenceName));
					}

				private:
					FString DataReferenceName;
			};

		public:


			TOutputNode(const FString& InNodeDescription, const FString& InVertexName)
			:	NodeDescription(InNodeDescription)
			,	VertexName(InVertexName)
			,	Factory(InVertexName)
			{
				FInputDataVertex InputVertex = MakeInputDataVertex<DataType>(VertexName, FText::GetEmpty());
				FDataVertexKey InputKey = MakeDataVertexKey(InputVertex);

				Inputs.Add(InputKey, InputVertex);

				FOutputDataVertex OutputVertex = MakeOutputDataVertex<DataType>(VertexName, FText::GetEmpty());
				FDataVertexKey OutputKey = MakeDataVertexKey(OutputVertex);

				Outputs.Add(OutputKey, OutputVertex);
			}

			virtual const FString& GetDescription() const override
			{
				return NodeDescription;
			}

			virtual const FName& GetClassName() const override
			{
				// TODO: Any special formatting for these node type names?
				static const FName ClassName = FName(FString(TEXT("Output_")) + FString(TDataReferenceTypeInfo<DataType>::TypeName));

				return ClassName;
			}

			virtual const FInputDataVertexCollection& GetInputDataVertices() const override
			{
				return Inputs;
			}

			virtual const FOutputDataVertexCollection& GetOutputDataVertices() const override
			{
				return Outputs;
			}

			virtual IOperatorFactory& GetDefaultOperatorFactory() override
			{
				return Factory;
			}

		private:
			FString NodeDescription;
			FString VertexName;

			FOutputOperatorFactory Factory;


			FInputDataVertexCollection Inputs;
			FOutputDataVertexCollection Outputs;
	};
}

#undef LOCTEXT_NAMESPACE //MetasoundOutputNode
