// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MetasoundBuilderInterface.h"
#include "MetasoundNode.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundNodeRegistrationMacro.h"
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
		METASOUNDFRONTEND_API const FVertexName& GetAddressInputName();
		METASOUNDFRONTEND_API const FVertexName& GetDefaultDataInputName();
		METASOUNDFRONTEND_API const FVertexName& GetOutputName();
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
				Info.DisplayName = METASOUND_LOCTEXT_FORMAT("Metasound_ReceiveNodeDisplayNameFormat", "Receive {0}", GetMetasoundDataTypeDisplayText<TDataType>());
				Info.Description = METASOUND_LOCTEXT("Metasound_ReceiveNodeDescription", "Receives data from a send node with the same name.");
				Info.Author = PluginAuthor;
				Info.PromptIfMissing = PluginNodeMissingPrompt;
				Info.DefaultInterface = DeclareVertexInterface();
				Info.CategoryHierarchy = { METASOUND_LOCTEXT("Metasound_TransmissionNodeCategory", "Transmission") };
				Info.Keywords = { };

				// Then send & receive nodes do not work as expected, particularly 
				// around multiple-consumer scenarios. Deprecate them to avoid
				// metasound assets from relying on send & receive nodes. 
				Info.bDeprecated = true; 

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
					, Receiver(nullptr)
				{
					Receiver = CreateNewReceiver();
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
					if (*SendAddress != CachedSendAddress)
					{
						ResetReceiverAndCleanupChannel();
						CachedSendAddress = *SendAddress;

						Receiver = CreateNewReceiver();
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

				FSendAddress GetSendAddressWithDataType(const FSendAddress& InAddress) const 
				{
					// The data type of a send address is inferred by the underlying
					// data type of this node. A full send address, including the data type,
					// cannot be constructed from a literal. 
					return FSendAddress{ InAddress.GetChannelName(), GetMetasoundDataTypeName<TDataType>(), InAddress.GetInstanceID() };
				}

				TReceiverPtr<TDataType> CreateNewReceiver() const
				{
					if (ensure(SendAddress->GetDataType().IsNone() || (GetMetasoundDataTypeName<TDataType>() == SendAddress->GetDataType())))
					{
						return FDataTransmissionCenter::Get().RegisterNewReceiver<TDataType>(GetSendAddressWithDataType(*SendAddress), CachedReceiverParams);
					}
					return TReceiverPtr<TDataType>(nullptr);
				}
				
				void ResetReceiverAndCleanupChannel()
				{
					Receiver.Reset();
					FDataTransmissionCenter::Get().UnregisterDataChannelIfUnconnected(GetSendAddressWithDataType(CachedSendAddress));
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
