// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundNode.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundDataReference.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundRouter.h"

#include <type_traits>

#define LOCTEXT_NAMESPACE "MetasoundFrontend"


namespace Metasound
{
	template<typename TDataType>
	class TSendNode : public FNode
	{
	public:
		static const FString& GetAddressInputName()
		{
			static const FString InputName = FString(TEXT("Address"));
			return InputName;
		}

		static const FString& GetSendInputName()
		{
			static const FString SendInput = GetMetasoundDataTypeString<TDataType>();
			return SendInput;
		}

		static FVertexInterface DeclareVertexInterface()
		{
			return FVertexInterface(
				FInputVertexInterface(
					TInputDataVertexModel<FSendAddress>(GetAddressInputName(), FText::GetEmpty()),
					TInputDataVertexModel<TDataType>(GetSendInputName(), FText::GetEmpty())
				),
				FOutputVertexInterface(
				)
			);
		}

		static const FNodeInfo& GetNodeInfo()
		{
			auto InitNodeInfo = []() -> FNodeInfo
			{
				FNodeInfo Info;

				Info.ClassName = FName(*FString::Printf(TEXT("Send %s"), *GetSendInputName()));
				Info.MajorVersion = 1;
				Info.MinorVersion = 0;
				Info.Description = LOCTEXT("Metasound_SendNodeDescription", "Sends data from a send node with the same name.");
				Info.Author = PluginAuthor;
				Info.PromptIfMissing = PluginNodeMissingPrompt;
				Info.DefaultInterface = DeclareVertexInterface();
				Info.CategoryHierarchy = {LOCTEXT("Metasound_SendNodeCategory", "Send"), FText::AsCultureInvariant(GetMetasoundDataTypeString<TDataType>())};
				Info.Keywords = {TEXT("send"), GetMetasoundDataTypeName<TDataType>()};

				return Info;
			};

			static const FNodeInfo Info = InitNodeInfo();

			return Info;
		}


	private:
		class TSendOperator : public TExecutableOperator<TSendOperator>
		{
			public:

				TSendOperator(TDataReadReference<TDataType> InInputData, TDataReadReference<FSendAddress> InSendAddress, const FOperatorSettings& InOperatorSettings)
					: InputData(InInputData)
					, SendAddress(InSendAddress)
					, CachedSendAddress(*InSendAddress)
					, CachedSenderParams({InOperatorSettings, 0.0f})
					, Sender(FDataTransmissionCenter::Get().RegisterNewSend<TDataType>(CachedSendAddress, CachedSenderParams))
				{
					Inputs.AddDataReadReference<FSendAddress>(GetAddressInputName(), SendAddress);
					Inputs.AddDataReadReference<TDataType>(GetSendInputName(), TDataReadReference<TDataType>(InputData));
				}

				virtual ~TSendOperator() {}

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
						Sender = FDataTransmissionCenter::Get().RegisterNewSend<TDataType>(CachedSendAddress, CachedSenderParams);
						check(Sender.IsValid());
					}

					Sender->Push(*InputData);
				}

			private:
				TDataReadReference<TDataType> InputData;
				TDataReadReference<FSendAddress> SendAddress;
				FSendAddress CachedSendAddress;
				FSenderInitParams CachedSenderParams;

				TSenderPtr<TDataType> Sender;

				FDataReferenceCollection Inputs;
				FDataReferenceCollection Outputs;
		};

		class FSendOperatorFactory : public IOperatorFactory
		{
			public:
				FSendOperatorFactory() = default;

				virtual TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override
				{
					return MakeUnique<TSendOperator>(InParams.InputDataReferences.GetDataReadReference<TDataType>(GetSendInputName()),
						InParams.InputDataReferences.GetDataReadReference<FSendAddress>(GetAddressInputName()),
						InParams.OperatorSettings
						);
				}
		};

		public:

			TSendNode(const FNodeInitData& InInitData)
				: FNode(InInitData.InstanceName, GetNodeInfo())
				, Interface(DeclareVertexInterface())
				, Factory(MakeOperatorFactoryRef<FSendOperatorFactory>())
			{
			}

			virtual ~TSendNode() = default;

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
