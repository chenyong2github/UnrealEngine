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
	// TODO: should consider making this a subclass of TInputNode. Both share requirement
	// of needing a default DataReference. The construction of that gets tricky so 
	// would be nice to consolidate all that logic. 
	//
	//
	// TODO: Maybe it could work like this...
	// Can instantiate a factory with default args. Factory is valid as long as constructor of data type supports
	// parameters (w/ or w/o operator settings.) Factory makes a lambda for creating data type references.
	// Factory gets instantiated when node is declared.  Basically lets us forward constructor 
	// args until the creation of the lambda, which requires a copy to capture "[=]". 
	//
	// But that still leaves the issue of setting those values on the node. Would be best to have that in NodeInitParams
	// but its not clear how that is passed about. Maybe something that checks whether it can construct from literal. 
	//
	// Or you could go another route where you check whether the Operator constructor takes a specific set of references and 
	// use that to auto-generate factories.  
	
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

		template<typename T>
		struct TReceiverOperatorFactoryHelper
		{
			using FDataTypeInfo = TDataReferenceTypeInfo<T>;

			static constexpr bool bUseDefaultCtor = FDataTypeInfo::bCanUseDefaultConstructor && !FDataTypeInfo::bIsConstructableWithSettings;
			static constexpr bool bUseSettingsCtor = FDataTypeInfo::bIsConstructableWithSettings;
		};

		class FReceiverOperatorFactory : public IOperatorFactory
		{
			public:
				FReceiverOperatorFactory() = default;

				template<
					typename U = TDataType,
					typename std::enable_if< TReceiverOperatorFactoryHelper<U>::bUseDefaultCtor, int>::type = 0
				>
				TDataWriteReference<TDataType> CreateDefaultWriteReference(const FCreateOperatorParams& InParams) const
				{
					return TDataWriteReference<TDataType>::CreateNew();
				}

				template<
					typename U = TDataType,
					typename std::enable_if< TReceiverOperatorFactoryHelper<U>::bUseSettingsCtor, int>::type = 0
				>
				TDataWriteReference<TDataType> CreateDefaultWriteReference(const FCreateOperatorParams& InParams) const
				{
					return TDataWriteReference<TDataType>::CreateNew(InParams.OperatorSettings);
				}


				virtual TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors) override
				{
					return MakeUnique<TReceiverOperator>(
						CreateDefaultWriteReference(InParams),
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
				, Factory(MakeOperatorFactoryRef<FReceiverOperatorFactory>())
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
