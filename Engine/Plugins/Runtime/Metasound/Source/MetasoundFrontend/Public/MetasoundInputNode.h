// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundNodeInterface.h"

#define LOCTEXT_NAMESPACE "MetasoundGraphCore"


namespace Metasound
{
	template<typename DataType>
	class TInputNode : public INode
	{
		static_assert(TDataReferenceTypeInfo<DataType>::bIsValidSpecialization, "Please use DECLARE_METASOUND_DATA_REFERENCE_TYPES with this class before trying to create an input node with it.");

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

		// TODO: instead of this copy factory, register a DataType factory lambda that uses a FDataTypeLiteralParam
		class FCopyOperatorFactory : public IOperatorFactory
		{
			static_assert(TIsConstructible<DataType, const DataType&>::Value, "Data Type must be copy constructible!");
				
			public:
				using FDataWriteReference = TDataWriteReference<DataType>;
				using FInputNodeType = TInputNode<DataType>;

				template< typename... ArgTypes >
				FCopyOperatorFactory(ArgTypes&&... Args)
				:	Data(Forward<ArgTypes>(Args)...)
				{
				}

				/*
				template<typename = typename TEnableIf< TIsConstructible<DataType>::Value >::Type >
				FCopyOperatorFactory()
				:	Data()
				{
				}
				*/

				FCopyOperatorFactory(FDataTypeLiteralParam InitParam, const FOperatorSettings& InSettings)
					: Data(InitParam.ParseTo<DataType>(InSettings))
				{
				}

				virtual TUniquePtr<IOperator> CreateOperator(const INode& InNode, const FOperatorSettings& InOperatorSettings, const FDataReferenceCollection& InInputDataReferences, TArray<TUniquePtr<IOperatorBuildError>>& OutErrors) override
				{
					// Use copy constructor to create a new parameter reference.
					FDataWriteReference DataRef = FDataWriteReference::CreateNew(Data);
					// TODO: Write special version of this for audio types since they will need to be constructed based upon the operator settings. 
					// TODO: Need to ponder how inputs are initialized, but template specialization might do the trick. 

					const FInputNodeType& InputNode = static_cast<const FInputNodeType&>(InNode);

					return MakeUnique<FInputOperator>(InputNode.GetVertexName(), DataRef);
				}

			private:
				DataType Data;
		};

		public:
			// Constructors that need to get working on linux:
			/*
			template<typename = typename TEnableIf< TIsConstructible<DataType>::Value >::Type>
			TInputNode(const FString& InNodeDescription, const FString& InVertexName)
			:	NodeDescription(InNodeDescription)
			,	VertexName(InVertexName)
			,   Factory()
			{
				FOutputDataVertex OutputVertex = MakeOutputDataVertex<DataType>(VertexName, FText::GetEmpty());
				FDataVertexKey OutputVertexKey = MakeDataVertexKey(OutputVertex);

				Outputs.Add(OutputVertexKey, OutputVertex);

				FInputDataVertex InputVertex = MakeInputDataVertex<DataType>(VertexName, FText::GetEmpty());
				FDataVertexKey InputVertexKey = MakeDataVertexKey(InputVertex);

				Inputs.Add(InputVertexKey, InputVertex);

			}
			*/

			template<typename... ArgTypes>
			TInputNode(const FString& InNodeDescription, const FString& InVertexName, ArgTypes&&... Args)
			:	NodeDescription(InNodeDescription)
			,	VertexName(InVertexName)
			,	Factory(Forward<ArgTypes>(Args)...)
			{
				static_assert(TIsConstructible<DataType, ArgTypes...>::Value, "Tried to construct TInputNode<DataType> with arguments that don't match any constructor for DataType.");

				FOutputDataVertex OutputVertex = MakeOutputDataVertex<DataType>(VertexName, FText::GetEmpty());
				FDataVertexKey OutputVertexKey = MakeDataVertexKey(OutputVertex);

				Outputs.Add(OutputVertexKey, OutputVertex);

				FInputDataVertex InputVertex = MakeInputDataVertex<DataType>(VertexName, FText::GetEmpty());
				FDataVertexKey InputVertexKey = MakeDataVertexKey(InputVertex);

				Inputs.Add(InputVertexKey, InputVertex);

			}

			explicit TInputNode(const FString& InNodeDescription, const FString& InVertexName, FDataTypeLiteralParam&& InParam, const FOperatorSettings& InSettings)
				: NodeDescription(InNodeDescription)
				, VertexName(InVertexName)
				, Factory(MoveTemp(InParam), InSettings)
			{
				FOutputDataVertex OutputVertex = MakeOutputDataVertex<DataType>(VertexName, FText::GetEmpty());
				FDataVertexKey OutputVertexKey = MakeDataVertexKey(OutputVertex);

				Outputs.Add(OutputVertexKey, OutputVertex);

				FInputDataVertex InputVertex = MakeInputDataVertex<DataType>(VertexName, FText::GetEmpty());
				FDataVertexKey InputVertexKey = MakeDataVertexKey(InputVertex);

				Inputs.Add(InputVertexKey, InputVertex);
			}

			virtual const FString& GetInstanceName() const override
			{
				return NodeDescription;
			}

			virtual const FName& GetClassName() const override
			{
				// TODO: although this is ok with MSVC's lax template instantiation, every other compiler will complain about TDataReferenceTypeInfo.
				//static const FName ClassName = FName(FString(TEXT("Input_")) + FString(TDataReferenceTypeInfo<DataType>::TypeName));
				static const FName ClassName("InputNode");
				return ClassName;
			}

			virtual const FText& GetDescription() const override
			{
				static const FText Description = LOCTEXT("Metasound_InputNodeDescription", "Input into the parent Metasound graph.");
				return Description;
			}

			virtual const FText& GetAuthorName() const override
			{
				return PluginAuthor;
			}

			virtual const FText& GetPromptIfMissing() const override
			{
				return PluginNodeMissingPrompt;
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
} // namespace Metasound
#undef LOCTEXT_NAMESPACE //MetasoundOutputNode
