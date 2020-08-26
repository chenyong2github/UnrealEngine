// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundDataReference.h"
#include "MetasoundExecutableOperator.h"

#include <type_traits>

#define LOCTEXT_NAMESPACE "MetasoundGraphCore"


namespace Metasound
{
	// This convenience node can be registered and will invoke static_cast<ToDataType>(FromDataType) every time it is executed.
	template<typename FromDataType, typename ToDataType>
	class TAutoConverterNode : public INode
	{
		static_assert(TDataReferenceTypeInfo<FromDataType>::bIsValidSpecialization, "Please use DECLARE_METASOUND_DATA_REFERENCE_TYPES with this class before trying to create an converter node with it.");
		static_assert(TDataReferenceTypeInfo<ToDataType>::bIsValidSpecialization, "Please use DECLARE_METASOUND_DATA_REFERENCE_TYPES with this class before trying to create an converter node with it.");
		static_assert(std::is_convertible<FromDataType, ToDataType>::value, "Tried to create an auto converter node between two types we can't static_cast between.");

	public:
		static FString& GetInputName()
		{
			static FString InputName = FString(TDataReferenceTypeInfo<FromDataType>::TypeName);
			return InputName;
		}

		static FString& GetOutputName()
		{
			static FString OutputName = FString(TDataReferenceTypeInfo<ToDataType>::TypeName);
			return OutputName;
		}

	private:
		class FConverterOperator : public TExecutableOperator<FConverterOperator>
		{
			public:

				FConverterOperator(TDataReadReference<FromDataType> InFromDataReference)
					: FromData(InFromDataReference)
					, ToData(TDataWriteReference<ToDataType>::CreateNew()) // TODO: this currently only works with default constructible datatypes. Can we do something similar to what we did with literals?
				{
					Outputs.AddDataReadReference<ToDataType>(GetOutputName(), TDataReadReference<ToDataType>(ToData));
				}

				virtual ~FConverterOperator() {}

				virtual const FDataReferenceCollection& GetInputs() const override
				{
					return Inputs;
				}

				virtual const FDataReferenceCollection& GetOutputs() const override
				{
					return Outputs;
				}

				void Execute()
				{
					*ToData = static_cast<ToDataType>(*FromData);
				}

			private:
				TDataReadReference<FromDataType> FromData;
				TDataWriteReference<ToDataType> ToData;

				FDataReferenceCollection Inputs;
				FDataReferenceCollection Outputs;
		};

		class FCoverterOperatorFactory : public IOperatorFactory
		{
			public:
				FCoverterOperatorFactory() = default;

				virtual TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override
				{
					if (!InParams.InputDataReferences.ContainsDataReadReference<FromDataType>(GetInputName()))
					{
						// TODO: Add build error.
						return TUniquePtr<IOperator>(nullptr);
					}

					return MakeUnique<FConverterOperator>(InParams.InputDataReferences.GetDataReadReference<FromDataType>(GetInputName()));
				}
		};

		public:
			static FVertexInterface DeclareVertexInterface()
			{
				return FVertexInterface(
					FInputVertexInterface(
						TInputDataVertexModel<FromDataType>(GetInputName(), FText::GetEmpty())
					),
					FOutputVertexInterface(
						TOutputDataVertexModel<ToDataType>(GetOutputName(), FText::GetEmpty())
					)
				);
			}

			TAutoConverterNode(const FNodeInitData& InInitData)
				: NodeDescription(InInitData.InstanceName)
				, Interface(DeclareVertexInterface())
				, Factory(MakeOperatorFactoryRef<FCoverterOperatorFactory>())
			{
			}

			virtual ~TAutoConverterNode() = default;

			virtual const FName& GetClassName() const override
			{
				static const FName ClassName(*FString::Printf(TEXT("%s To %s"), *GetInputName(), *GetOutputName()));
				return ClassName;
			}

			virtual const FString& GetInstanceName() const override
			{
				return NodeDescription;
			}

			virtual const FText& GetDescription() const override
			{
				static const FText Description = LOCTEXT("Metasound_ConverterNodeDescription", "Converts between two different data types.");
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

			virtual FOperatorFactorySharedRef GetDefaultOperatorFactory() const override
			{
				return Factory;
			}

		private:
			FString NodeDescription;
			FVertexInterface Interface;
			FOperatorFactorySharedRef Factory;
			
	};
}
#undef LOCTEXT_NAMESPACE
