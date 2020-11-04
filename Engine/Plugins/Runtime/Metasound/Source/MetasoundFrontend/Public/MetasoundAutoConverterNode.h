// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundBuildError.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundDataFactory.h"
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
		static_assert(std::is_convertible<FromDataType, ToDataType>::value, "Tried to create an auto converter node between two types we can't static_cast between.");

	public:
		static const FString& GetInputName()
		{
			static const FString InputName = GetMetasoundDataTypeString<FromDataType>();
			return InputName;
		}

		static const FString& GetOutputName()
		{
			static const FString OutputName = GetMetasoundDataTypeString<ToDataType>();
			return OutputName;
		}

	private:
		/** FConverterOperator converts from "FromDataType" to "ToDataType" using
		 * a implicit conversion operators. 
		 */
		class FConverterOperator : public TExecutableOperator<FConverterOperator>
		{
			public:

				FConverterOperator(TDataReadReference<FromDataType> InFromDataReference, TDataWriteReference<ToDataType> InToDataReference)
					: FromData(InFromDataReference)
					, ToData(InToDataReference) 
				{
					Outputs.AddDataReadReference<ToDataType>(GetOutputName(), ToData);
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

		/** FConverterOperatorFactory creates an operator which converts from 
		 * "FromDataType" to "ToDataType". 
		 */
		class FCoverterOperatorFactory : public IOperatorFactory
		{
			public:
				FCoverterOperatorFactory() = default;

				virtual TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override
				{
					TDataWriteReference<ToDataType> WriteReference = TDataWriteReferenceFactory<ToDataType>::CreateAny(InParams.OperatorSettings);

					if (!InParams.InputDataReferences.ContainsDataReadReference<FromDataType>(GetInputName()))
					{
						if (ensure(InParams.Node.GetVertexInterface().ContainsInputVertex(GetInputName())))
						{
							// There should be something hooked up to the converter node. Report it as an error. 
							FInputDataDestination Dest(InParams.Node, InParams.Node.GetVertexInterface().GetInputVertex(GetInputName()));
							AddBuildError<FMissingInputDataReferenceError>(OutErrors, Dest);
						}

						// We can still build something even though there is an error. 
						TDataReadReference<FromDataType> ReadReference = TDataReadReferenceFactory<FromDataType>::CreateAny(InParams.OperatorSettings);
						return MakeUnique<FConverterOperator>(ReadReference, WriteReference);
					}

					TDataReadReference<FromDataType> ReadReference = InParams.InputDataReferences.GetDataReadReference<FromDataType>(GetInputName());

					return MakeUnique<FConverterOperator>(ReadReference, WriteReference);
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
