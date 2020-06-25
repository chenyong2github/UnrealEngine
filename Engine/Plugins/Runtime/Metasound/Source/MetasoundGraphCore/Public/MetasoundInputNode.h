// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundNodeInterface.h"

namespace Metasound
{
	template<typename DataType>
	class TInputNode : public INode
	{
		
			class FInputOperator : public IOperator
			{
				public:
					using FDataWriteReference = TDataWriteReference<DataType>;

					FInputOperator(const FString& InDataReferenceName, FDataWriteReference InDataReference)
					{
						Inputs.AddDataWriteReference<DataType>(InDataReferenceName, InDataReference);
						Outputs.AddDataReadReference<DataType>(InDataReferenceName, InDataReference);
					}

					virtual ~FInputOperator() {}

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
					FDataReferenceCollection Outputs;
					FDataReferenceCollection Inputs;
			};

			// TODO: Would be nice if we didn't utilize the copy constructor by default here. But it's a nice catch-all
			// for our use case.  Could store constructor parameters of the paramtype, but run the risk of holding rvalues
			// or pointers to objects which are no longer valid. 
			class FCopyOperatorFactory : public IOperatorFactory
			{
				public:
					using FDataWriteReference = TDataWriteReference<DataType>;
					using FInputNodeType = TInputNode<DataType>;

					template<typename... ArgTypes, typename = typename TEnableIf< TIsConstructible<DataType, ArgTypes...>::Value >::Type >
					FCopyOperatorFactory(ArgTypes&&... Args)
					:	Data(Forward<ArgTypes>(Args)...)
					{
					}

					template<typename = typename TEnableIf< TIsConstructible<DataType>::Value >::Type >
					FCopyOperatorFactory()
					:	Data()
					{
					}

					virtual TUniquePtr<IOperator> CreateOperator(const INode& InNode, const FOperatorSettings& InOperatorSettings, const FDataReferenceCollection& InInputDataReferences, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors) override
					{
						// Use copy constructor to create a new parameter reference.
						FDataWriteReference DataRef(Data);
						// TODO: Write special version of this for audio types since they will need to be constructed based upon the operator settings. 
						// TODO: Need to ponder how inputs are initialized, but template specialization might do the trick. 

						const FInputNodeType& InputNode = static_cast<const FInputNodeType&>(InNode);

						return MakeUnique<FInputOperator>(InputNode.GetVertexName(), DataRef);
					}

				private:
					DataType Data;
			};

		public:


			template<typename = typename TEnableIf< TIsConstructible<DataType>::Value >::Type >
			TInputNode(const FString& InNodeDescription, const FString& InVertexName)
			:	NodeDescription(InNodeDescription)
			,	VertexName(InVertexName)
			{
				FOutputDataVertex OutputVertex = MakeOutputDataVertex<DataType>(VertexName, FText::GetEmpty());
				FDataVertexKey OutputVertexKey = MakeDataVertexKey(OutputVertex);

				Outputs.Add(OutputVertexKey, OutputVertex);

				FInputDataVertex InputVertex = MakeInputDataVertex<DataType>(VertexName, FText::GetEmpty());
				FDataVertexKey InputVertexKey = MakeDataVertexKey(InputVertex);

				Inputs.Add(InputVertexKey, InputVertex);

			}

			template<typename... ArgTypes, typename = typename TEnableIf< TIsConstructible<DataType, ArgTypes...>::Value >::Type >
			TInputNode(const FString& InNodeDescription, const FString& InVertexName, ArgTypes&&... Args)
			:	NodeDescription(InNodeDescription)
			,	VertexName(InVertexName)
			,	Factory(Forward<ArgTypes>(Args)...)
			{
				FOutputDataVertex OutputVertex = MakeOutputDataVertex<DataType>(VertexName, FText::GetEmpty());
				FDataVertexKey OutputVertexKey = MakeDataVertexKey(OutputVertex);

				Outputs.Add(OutputVertexKey, OutputVertex);

				FInputDataVertex InputVertex = MakeInputDataVertex<DataType>(VertexName, FText::GetEmpty());
				FDataVertexKey InputVertexKey = MakeDataVertexKey(InputVertex);

				Inputs.Add(InputVertexKey, InputVertex);

			}

			virtual const FString& GetDescription() const override
			{
				return NodeDescription;
			}

			virtual const FName& GetClassName() const override
			{
				static const FName ClassName = FName(FString(TEXT("Input_")) + FString(TDataReferenceTypeInfo<DataType>::TypeName));

				return ClassName;
			}

			const FString& GetVertexName() const
			{
				return VertexName;
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

			FCopyOperatorFactory Factory;

			FInputDataVertexCollection Inputs;
			FOutputDataVertexCollection Outputs;
	};
}
