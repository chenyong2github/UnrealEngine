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
	namespace ReceiveNodeInfo
	{
		METASOUNDFRONTEND_API const FString& GetAddressInputName();
		METASOUNDFRONTEND_API const FString& GetDefaultDataInputName();
		METASOUNDFRONTEND_API const FString& GetOutputName();
		METASOUNDFRONTEND_API FNodeClassName GetClassNameForDataType(const FName& InDataTypeName);
		METASOUNDFRONTEND_API int32 GetCurrentMajorVersion();
		METASOUNDFRONTEND_API int32 GetCurrentMinorVersion();
	};

	template<typename TDataType>
	class TReceiveNode : public FNode
	{
	public:
		static FVertexInterface DeclareVertexInterface()
		{
			return FVertexInterface(
				FInputVertexInterface(
					TInputDataVertexModel<FSendAddress>(ReceiveNodeInfo::GetAddressInputName(), FText::GetEmpty()),
					TInputDataVertexModel<TDataType>(ReceiveNodeInfo::GetDefaultDataInputName(), FText::GetEmpty())
				),
				FOutputVertexInterface(
					TOutputDataVertexModel<TDataType>(ReceiveNodeInfo::GetOutputName(), FText::GetEmpty())
				)
			);
		}

		static const FNodeClassMetadata& GetNodeInfo()
		{
			auto InitNodeInfo = []() -> FNodeClassMetadata
			{
				FNodeClassMetadata Info;
				Info.ClassName = ReceiveNodeInfo::GetClassNameForDataType(GetMetasoundDataTypeName<TDataType>());
				Info.MajorVersion = ReceiveNodeInfo::GetCurrentMajorVersion();
				Info.MinorVersion = ReceiveNodeInfo::GetCurrentMinorVersion();
				Info.DisplayName = FText::Format(LOCTEXT("Metasound_ReceiveNodeDisplayNameFormat", "Receive {0}"), GetMetasoundDataTypeDisplayText<TDataType>());
				Info.Description = LOCTEXT("Metasound_ReceiveNodeDescription", "Receives data from a send node with the same name.");
				Info.Author = PluginAuthor;
				Info.PromptIfMissing = PluginNodeMissingPrompt;
				Info.DefaultInterface = DeclareVertexInterface();
				Info.CategoryHierarchy = { LOCTEXT("Metasound_TransmissionNodeCategory", "Transmission") };
				Info.Keywords = { };

				return Info;
			};

			static const FNodeClassMetadata Info = InitNodeInfo();

			return Info;
		}

	private:
		class TReceiverOperator : public TExecutableOperator<TReceiverOperator>
		{
			TReceiverOperator() = delete;

			public:
				TReceiverOperator(TDataReadReference<TDataType> InInitDataRef, TDataWriteReference<TDataType> InOutDataRef, TDataReadReference<FSendAddress> InSendAddress, const FOperatorSettings& InOperatorSettings)
					: bHasNotReceivedData(true)
					, DefaultData(InInitDataRef)
					, OutputData(InOutDataRef)
					, SendAddress(InSendAddress)
					, CachedSendAddress(*InSendAddress)
					, CachedReceiverParams({InOperatorSettings})
					, Receiver(FDataTransmissionCenter::Get().RegisterNewReceiver<TDataType>(CachedSendAddress, CachedReceiverParams))
				{
				}

				virtual ~TReceiverOperator() 
				{
					ResetReceiverAndCleanupChannel();
				}

				virtual FDataReferenceCollection GetInputs() const override
				{
					FDataReferenceCollection Inputs;

					Inputs.AddDataReadReference<TDataType>(ReceiveNodeInfo::GetDefaultDataInputName(), DefaultData);
					Inputs.AddDataReadReference<FSendAddress>(ReceiveNodeInfo::GetAddressInputName(), SendAddress);

					return Inputs;
				}

				virtual FDataReferenceCollection GetOutputs() const override
				{
					FDataReferenceCollection Outputs;
					Outputs.AddDataReadReference<TDataType>(ReceiveNodeInfo::GetOutputName(), TDataReadReference<TDataType>(OutputData));
					return Outputs;
				}

				void Execute()
				{
					if (SendAddress->ChannelName != CachedSendAddress.ChannelName)
					{
						ResetReceiverAndCleanupChannel();
						CachedSendAddress = *SendAddress;
						Receiver = FDataTransmissionCenter::Get().RegisterNewReceiver<TDataType>(CachedSendAddress, CachedReceiverParams);
					}

					bool bHasNewData = false;
					if (ensure(Receiver.IsValid()))
					{
						bHasNewData = Receiver->CanPop();
						if (bHasNewData)
						{
							Receiver->Pop(*OutputData);
							bHasNotReceivedData = false;
						}
					}

					if (bHasNotReceivedData)
					{
						*OutputData = *DefaultData;
						bHasNewData = true;
					}

					if (TExecutableDataType<TDataType>::bIsExecutable)
					{
						TExecutableDataType<TDataType>::ExecuteInline(*OutputData, bHasNewData);
					}
				}

			private:

				void ResetReceiverAndCleanupChannel()
				{
					Receiver.Reset();
					FDataTransmissionCenter::Get().UnregisterDataChannelIfUnconnected(GetMetasoundDataTypeName<TDataType>(), CachedSendAddress);
				}

				bool bHasNotReceivedData;

				TDataReadReference<TDataType> DefaultData;
				TDataWriteReference<TDataType> OutputData;
				TDataReadReference<FSendAddress> SendAddress;

				FSendAddress CachedSendAddress;
				FReceiverInitParams CachedReceiverParams;

				TReceiverPtr<TDataType> Receiver;
		};

		class FReceiverOperatorFactory : public IOperatorFactory
		{
			public:
				FReceiverOperatorFactory() = default;

				virtual TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override
				{
					TDataReadReference<TDataType> DefaultReadRef = TDataReadReferenceFactory<TDataType>::CreateAny(InParams.OperatorSettings);

					if (InParams.InputDataReferences.ContainsDataReadReference<TDataType>(ReceiveNodeInfo::GetDefaultDataInputName()))
					{
						DefaultReadRef = InParams.InputDataReferences.GetDataReadReference<TDataType>(ReceiveNodeInfo::GetDefaultDataInputName());
					}

					return MakeUnique<TReceiverOperator>(
						DefaultReadRef,
						TDataWriteReferenceFactory<TDataType>::CreateAny(InParams.OperatorSettings, *DefaultReadRef),
						InParams.InputDataReferences.GetDataReadReferenceOrConstruct<FSendAddress>(ReceiveNodeInfo::GetAddressInputName()),
						InParams.OperatorSettings
						);
				}
		};

		public:

			TReceiveNode(const FNodeInitData& InInitData)
				: FNode(InInitData.InstanceName, InInitData.InstanceID, GetNodeInfo())
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
