// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundDataReference.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundRouter.h"

#include <type_traits>

#define LOCTEXT_NAMESPACE "MetasoundGraphCore"


namespace Metasound
{
	// This convenience node can be registered and will invoke static_cast<ToDataType>(FromDataType) every time it is executed.
	template<typename TDataType>
	class TReceiveNode : public INode
	{
		static_assert(TDataReferenceTypeInfo<TDataType>::bIsValidSpecialization, "Please use DECLARE_METASOUND_DATA_REFERENCE_TYPES with this class before trying to create an converter node with it.");

	public:
		static FString& GetAddressInputName()
		{
			static FString InputName = FString(TEXT("Address"));
			return InputName;
		}

		static FString& GetSendOutputName()
		{
			static FString SendInput = FString(TDataReferenceTypeInfo<TDataType>::TypeName);
			return SendInput;
		}

	private:
		class TReceiverOperator : public TExecutableOperator<TReceiverOperator>
		{
			public:

				TReceiverOperator(TDataReadReference<FSendAddress> InSendAddress, const FOperatorSettings& InOperatorSettings)
					: OutputData(TDataWriteReference<TDataType>::CreateNew())
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

		class FSendOperatorFactory : public IOperatorFactory
		{
			public:
				FSendOperatorFactory() = default;

				virtual TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override
				{
					return MakeUnique<TReceiverOperator>(
						InParams.InputDataReferences.GetDataReadReference<FSendAddress>(GetAddressInputName()),
						InParams.OperatorSettings
						);
				}
		};

		public:

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

			TReceiveNode(const FNodeInitData& InInitData)
				: NodeDescription(InInitData.InstanceName)
				, Interface(DeclareVertexInterface())
				, Factory(MakeOperatorFactoryRef<FSendOperatorFactory>())
			{
			}

			virtual ~TReceiveNode() = default;

			virtual const FName& GetClassName() const override
			{
				static const FName ClassName(*FString::Printf(TEXT("Receive %s"), *GetSendOutputName()));
				return ClassName;
			}

			virtual const FString& GetInstanceName() const override
			{
				return NodeDescription;
			}

			virtual const FText& GetDescription() const override
			{
				static const FText Description = LOCTEXT("Metasound_ReceiveNodeDescription", "receives data from a send node with the same name.");
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
