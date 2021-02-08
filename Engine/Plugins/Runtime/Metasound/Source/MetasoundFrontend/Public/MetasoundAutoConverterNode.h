// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundBuildError.h"
#include "MetasoundNode.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundDataFactory.h"
#include "MetasoundDataReference.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundFrontend.h"

#include <type_traits>

#define LOCTEXT_NAMESPACE "MetasoundFrontend"

namespace Metasound
{
	// This convenience node can be registered and will invoke static_cast<ToDataType>(FromDataType) every time it is executed.
	template<typename FromDataType, typename ToDataType>
	class TAutoConverterNode : public FNode
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

		static const FNodeClassMetadata& GetAutoConverterNodeMetadata()
		{
			auto InitNodeInfo = []() -> FNodeClassMetadata
			{
				const FString& InputDisplayName = GetInputName();
				const FString& OutputDisplayName = GetOutputName();

				FNodeDisplayStyle DisplayStyle;
				DisplayStyle.bShowName = false;
				DisplayStyle.bShowInputNames = false;
				DisplayStyle.bShowOutputNames = false;

				FNodeClassMetadata Info;
				Info.ClassName = { TEXT("Convert"), GetMetasoundDataTypeName<ToDataType>(), GetMetasoundDataTypeName<FromDataType>() };
				Info.MajorVersion = 1;
				Info.MinorVersion = 0;
				Info.DisplayName = FText::Format(LOCTEXT("Metasound_AutoConverterNodeDisplayNameFormat", "{0} to {1}"), FText::FromName(GetMetasoundDataTypeName<FromDataType>()), FText::FromName(GetMetasoundDataTypeName<ToDataType>()));
				Info.Description = LOCTEXT("Metasound_ConverterNodeDescription", "Converts between two different data types.");
				Info.Author = PluginAuthor;
				Info.DisplayStyle = DisplayStyle;
				Info.PromptIfMissing = PluginNodeMissingPrompt;
				Info.DefaultInterface = DeclareVertexInterface();

				Info.CategoryHierarchy = { LOCTEXT("Metasound_ConvertNodeCategory", "Conversions") };
				Info.Keywords = { "Convert", GetMetasoundDataTypeName<FromDataType>(), GetMetasoundDataTypeName<ToDataType>()};

				return Info;
			};

			static const FNodeClassMetadata Info = InitNodeInfo();

			return Info;
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
				}

				virtual ~FConverterOperator() {}

				virtual FDataReferenceCollection GetInputs() const override
				{
					FDataReferenceCollection Inputs;
					Inputs.AddDataReadReference<FromDataType>(GetInputName(), FromData);
					return Inputs;
				}

				virtual FDataReferenceCollection GetOutputs() const override
				{
					FDataReferenceCollection Outputs;
					Outputs.AddDataReadReference<ToDataType>(GetOutputName(), ToData);
					return Outputs;
				}

				void Execute()
				{
					*ToData = static_cast<ToDataType>(*FromData);
				}

			private:
				TDataReadReference<FromDataType> FromData;
				TDataWriteReference<ToDataType> ToData;
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

			TAutoConverterNode(const FNodeInitData& InInitData)
				: FNode(InInitData.InstanceName, InInitData.InstanceID, GetAutoConverterNodeMetadata())
				, Interface(DeclareVertexInterface())
				, Factory(MakeOperatorFactoryRef<FCoverterOperatorFactory>())
			{
			}

			virtual ~TAutoConverterNode() = default;

			virtual const FVertexInterface& GetVertexInterface() const override
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
			FVertexInterface Interface;
			FOperatorFactorySharedRef Factory;
	};
}
#undef LOCTEXT_NAMESPACE
