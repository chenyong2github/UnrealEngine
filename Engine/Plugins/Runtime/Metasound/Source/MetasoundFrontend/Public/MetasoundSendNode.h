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
	class TSendNode : public INode
	{
		static_assert(TDataReferenceTypeInfo<TDataType>::bIsValidSpecialization, "Please use DECLARE_METASOUND_DATA_REFERENCE_TYPES with this class before trying to create an converter node with it.");

	public:
		static FString& GetAddressInputName()
		{
			static FString InputName = FString(TEXT("Address"));
			return InputName;
		}

		static FString& GetSendInputName()
		{
			static FString SendInput = FString(TDataReferenceTypeInfo<TDataType>::TypeName);
			return SendInput;
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

			TSendNode(const FNodeInitData& InInitData)
				: NodeDescription(InInitData.InstanceName)
				, Interface(DeclareVertexInterface())
				, Factory(MakeOperatorFactoryRef<FSendOperatorFactory>())
			{
			}

			virtual ~TSendNode() = default;

			virtual const FName& GetClassName() const override
			{
				static const FName ClassName(*FString::Printf(TEXT("Send %s"), *GetSendInputName()));
				return ClassName;
			}

			virtual const FString& GetInstanceName() const override
			{
				return NodeDescription;
			}

			virtual const FText& GetDescription() const override
			{
				static const FText Description = LOCTEXT("Metasound_SendNodeDescription", "Sends data to any other metasound anywhere else.");
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
