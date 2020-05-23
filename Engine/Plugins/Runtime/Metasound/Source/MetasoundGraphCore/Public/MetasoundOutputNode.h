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

						return MakeUnique<FOutputOperator>(InNode.GetDescription(), InInputDataReferences.GetDataReadReference<DataType>(DataReferenceName));
					}

				private:
					FString DataReferenceName;
			};

		public:


			template<typename = typename TEnableIf< TIsConstructible<DataType>::Value >::Type >
			TOutputNode(const FString& InDescription)
			:	Description(InDescription)
			,	Factory(InDescription)
			{
				Outputs.Add(MakeOutputDataVertexDescription<DataType>(Description, FText::GetEmpty()));
				Inputs.Add(MakeInputDataVertexDescription<DataType>(Description, FText::GetEmpty()));
			}

			template<typename... ArgTypes, typename = typename TEnableIf< TIsConstructible<DataType, ArgTypes...>::Value >::Type >
			TOutputNode(const FString& InDescription, ArgTypes&&... Args)
			:	Description(InDescription)
			,	Factory(InDescription)
			,	Data(Forward<ArgTypes>(Args)...)
			{
				Outputs.Add(MakeOutputDataVertexDescription<DataType>(Description, FText::GetEmpty()));
				Inputs.Add(MakeInputDataVertexDescription<DataType>(Description, FText::GetEmpty()));
			}

			virtual const FString& GetDescription() const override
			{
				return Description;
			}

			virtual const FName& GetClassName() const override
			{
				// TODO: Any special formatting for these node type names?
				static const FName ClassName = FName(FString(TEXT("Output_")) + FString(TDataReferenceTypeInfo<DataType>::TypeName));

				return ClassName;
			}

			virtual const TArray<FInputDataVertexDescription>& GetInputs() const override
			{
				return Inputs;
			}

			virtual const TArray<FOutputDataVertexDescription>& GetOutputs() const override
			{
				return Outputs;
			}

			virtual IOperatorFactory& GetDefaultOperatorFactory() override
			{
				return Factory;
			}

		private:
			FString Description;

			FOutputOperatorFactory Factory;

			DataType Data;

			TArray<FInputDataVertexDescription> Inputs;
			TArray<FOutputDataVertexDescription> Outputs;
	};
}

#undef LOCTEXT_NAMESPACE //MetasoundOutputNode
