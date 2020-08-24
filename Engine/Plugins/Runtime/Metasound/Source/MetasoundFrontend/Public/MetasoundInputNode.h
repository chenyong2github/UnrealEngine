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
	class TInputNode : public FNode
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

		public:

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

				virtual TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override
				{
					// Use copy constructor to create a new parameter reference.
					FDataWriteReference DataRef = FDataWriteReference::CreateNew(Data);
					// TODO: Write special version of this for audio types since they will need to be constructed based upon the operator settings. 
					// TODO: Need to ponder how inputs are initialized, but template specialization might do the trick. 

					const FInputNodeType& InputNode = static_cast<const FInputNodeType&>(InParams.Node);

					return MakeUnique<FInputOperator>(InputNode.GetVertexName(), DataRef);
				}

			private:
				DataType Data;
		};
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

			static const FNodeInfo& GetNodeInfo()
			{
				static const FString ClassNameString = FString(TEXT("Input_")) + GetMetasoundDataTypeName<DataType>().ToString();
				static const FName ClassName(*ClassNameString);

				static const FNodeInfo Info = {
					ClassName,
					LOCTEXT("Metasound_InputNodeDescription", "Input into the parent Metasound graph."),
					PluginAuthor,
					PluginNodeMissingPrompt
				};

				return Info;
			}

			static FVertexInterface DeclareVertexInterface(const FString& InVertexName)
			{
				return FVertexInterface(
					FInputVertexInterface(
						TInputDataVertexModel<DataType>(InVertexName, FText::GetEmpty())
					),
					FOutputVertexInterface(
						TOutputDataVertexModel<DataType>(InVertexName, FText::GetEmpty())
					)
				);
			}

			template<typename... ArgTypes>
			TInputNode(const FString& InNodeDescription, const FString& InVertexName, ArgTypes&&... Args)
			:	FNode(InNodeDescription, GetNodeInfo())
			,	VertexName(InVertexName)
			,	Interface(DeclareVertexInterface(InVertexName))
			,	Factory(MakeOperatorFactoryRef<FCopyOperatorFactory>(Forward<ArgTypes>(Args)...))
			{
				static_assert(TIsConstructible<DataType, ArgTypes...>::Value, "Tried to construct TInputNode<DataType> with arguments that don't match any constructor for DataType.");
			}

			explicit TInputNode(const FString& InNodeDescription, const FString& InVertexName, FDataTypeLiteralParam&& InParam, const FOperatorSettings& InSettings)
			:	FNode(InNodeDescription, GetNodeInfo())
			,	VertexName(InVertexName)
			,	Interface(DeclareVertexInterface(InVertexName))
			, 	Factory(MakeOperatorFactoryRef<FCopyOperatorFactory>(MoveTemp(InParam), InSettings))
			{
			}


			const FString& GetVertexName() const
			{
				return VertexName;
			}

			virtual const FVertexInterface& GetVertexInterface() const override
			{
				return Interface;
			}

			virtual const FVertexInterface& GetDefaultVertexInterface() const override
			{
				return Interface;
			}

			virtual bool SetVertexInterface(const FVertexInterface& InInterface) override
			{
				return Interface == InInterface;
			}

			virtual bool IsVertexInterfaceSupported(const FVertexInterface& InInterface) const override
			{
				return Interface == InInterface;
			}

			virtual TSharedRef<IOperatorFactory, ESPMode::ThreadSafe> GetDefaultOperatorFactory() const override
			{
				return Factory;
			}

		private:
			FString VertexName;

			FVertexInterface Interface;
			FOperatorFactorySharedRef Factory;

	};
} // namespace Metasound
#undef LOCTEXT_NAMESPACE //MetasoundOutputNode
