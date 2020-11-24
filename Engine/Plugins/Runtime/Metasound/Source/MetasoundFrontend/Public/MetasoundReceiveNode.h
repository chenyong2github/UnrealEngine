// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundNode.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundDataFactory.h"
#include "MetasoundDataReference.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundRouter.h"

#include <type_traits>

#define LOCTEXT_NAMESPACE "MetasoundFrontend"


namespace Metasound
{
	template<typename TDataType>
	class TReceiveNode : public FNode
	{

	public:
		static const FString& GetAddressInputName()
		{
			static const FString InputName = FString(TEXT("Address"));
			return InputName;
		}

		static const FString& GetSendOutputName()
		{
			static const FString SendInput = GetMetasoundDataTypeString<TDataType>();
			return SendInput;
		}

		static FVertexInterface DeclareVertexInterface()
		{
			return FVertexInterface(
				FInputVertexInterface(
					TInputDataVertexModel<FSendAddress>(GetAddressInputName(), FText::GetEmpty())
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<TDataType>(GetSendOutputName(), FText::GetEmpty())
				)
			);
		}

		static const FNodeInfo& GetNodeInfo()
		{
			auto InitNodeInfo = []() -> FNodeInfo
			{
				FNodeInfo Info;
				Info.ClassName = FName(*FString::Printf(TEXT("Receive %s"), *GetSendOutputName()));
				Info.MajorVersion = 1;
				Info.MinorVersion = 0;
				Info.Description = LOCTEXT("Metasound_ReceiveNodeDescription", "Receives data from a send node with the same name.");
				Info.Author = PluginAuthor;
				Info.PromptIfMissing = PluginNodeMissingPrompt;
				Info.DefaultInterface = DeclareVertexInterface();
				Info.CategoryHierarchy = {LOCTEXT("Metasound_ReceiveNodeCategory", "Receive"), FText::AsCultureInvariant(GetMetasoundDataTypeString<TDataType>())};
				Info.Keywords = {TEXT("receive"), GetMetasoundDataTypeName<TDataType>()};

				return Info;
			};

			static const FNodeInfo Info = InitNodeInfo();

			return Info;
		}

	private:
		class TReceiverOperator : public TExecutableOperator<TReceiverOperator>
		{
				TReceiverOperator() = delete;
			public:

				TReceiverOperator(const TDataWriteReference<TDataType>& Data, TDataReadReference<FSendAddress> InSendAddress, const FOperatorSettings& InOperatorSettings)
					: OutputData(Data)
					, SendAddress(InSendAddress)
					, CachedSendAddress(*InSendAddress)
					, CachedReceiverParams({InOperatorSettings})
					, Receiver(FDataTransmissionCenter::Get().RegisterNewReceiver<TDataType>(CachedSendAddress, CachedReceiverParams))
				{
					Outputs.AddDataReadReference<TDataType>(GetSendOutputName(), TDataReadReference<TDataType>(OutputData));
				}

				virtual ~TReceiverOperator() {}

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
					if (SendAddress->ChannelName != CachedSendAddress.ChannelName)
					{
						CachedSendAddress = *SendAddress;
						Receiver = FDataTransmissionCenter::Get().RegisterNewReceiver<TDataType>(CachedSendAddress, CachedReceiverParams);
						check(Receiver.IsValid());
					}

					if (Receiver->CanPop())
					{
						Receiver->Pop(*OutputData);
					}
				}

			private:
				TDataWriteReference<TDataType> OutputData;
				TDataReadReference<FSendAddress> SendAddress;
				FSendAddress CachedSendAddress;
				FReceiverInitParams CachedReceiverParams;

				TReceiverPtr<TDataType> Receiver;

				FDataReferenceCollection Inputs;
				FDataReferenceCollection Outputs;
		};

		class FReceiverOperatorFactory : public IOperatorFactory
		{
			public:
				FReceiverOperatorFactory() = default;

				virtual TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override
				{
					return MakeUnique<TReceiverOperator>(
						TDataWriteReferenceFactory<TDataType>::CreateAny(InParams.OperatorSettings),
						InParams.InputDataReferences.GetDataReadReference<FSendAddress>(GetAddressInputName()),
						InParams.OperatorSettings
						);
				}
		};

		public:

			// TODO: default value of received object.
			TReceiveNode(const FNodeInitData& InInitData)
				: FNode(InInitData.InstanceName, GetNodeInfo())
				, Interface(DeclareVertexInterface())
				, Factory(MakeOperatorFactoryRef<FReceiverOperatorFactory>())
			{
			}

			virtual ~TReceiveNode() = default;

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
