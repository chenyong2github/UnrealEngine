// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundNodeInterface.h"

#define LOCTEXT_NAMESPACE "MetasoundOutputNode"

namespace Metasound
{
	template<typename DataType>
	class TOutputNode : public INode
	{
			static_assert(TDataReferenceTypeInfo<DataType>::bIsValidSpecialization, "Please use DECLARE_METASOUND_DATA_REFERENCE_TYPES with this class before trying to create an output node with it.");
			
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

			virtual const FName& GetClassName() const override
			{
				// TODO: Any special formatting for these node type names?
				// TODO: although this is ok with MSVC's lax template instantiation, every other compiler will complain about TDataReferenceTypeInfo.
				//static const FName ClassName = FName(FString(TEXT("Input_")) + FString(TDataReferenceTypeInfo<DataType>::TypeName));
				static const FName ClassName = FName(TEXT("OutputNode"));

				return ClassName;
			}

			virtual const FString& GetInstanceName() const override
			{
				return NodeDescription;
			}

			virtual const FString& GetDescription() const override
			{
				static FString Description = TEXT("This node can be used as an output for a given type.");
				return Description;
			}

			virtual const FString& GetAuthorName() const override
			{
				static FString Author = TEXT("Epic Games");
				return Author;
			}

			virtual const FString& GetPromptIfMissing() const override
			{
				static FString Prompt = TEXT("Make sure that the Metasound plugin is loaded.");
				return Prompt;
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
