// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IAudioExtensionPlugin.h"
#include "MetasoundDataReference.h"
#include "MetasoundLog.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundPrimitives.h"
#include "MetasoundRouter.h"

namespace Metasound
{
	class METASOUNDFRONTEND_API FMetasoundInstanceTransmitter : public IAudioInstanceTransmitter
	{
		FMetasoundInstanceTransmitter(const FMetasoundInstanceTransmitter&) = delete;
		FMetasoundInstanceTransmitter& operator=(const FMetasoundInstanceTransmitter&) = delete;
	public:
		struct FSendInfo
		{
			FSendAddress Address;
			FName ParameterName;
			FName TypeName;
		};

		struct FInitParams
		{
			FOperatorSettings OperatorSettings;
			uint64 InstanceID;
			TArray<FSendInfo> Infos;

			FInitParams(const FOperatorSettings& InSettings, uint64 InInstanceID, const TArray<FSendInfo>& InInfos=TArray<FSendInfo>())
			: OperatorSettings(InSettings)
			, InstanceID(InInstanceID)
			, Infos(InInfos)
			{
			}

		};

		FMetasoundInstanceTransmitter(const FMetasoundInstanceTransmitter::FInitParams& InInitParams);
		virtual ~FMetasoundInstanceTransmitter() = default;

		uint64 GetInstanceID() const override;

		bool SetFloatParameter(const FName& InParameterName, float InValue) override;
		bool SetIntParameter(const FName& InParameterName, int32 InValue) override;
		bool SetBoolParameter(const FName& InParameterName, bool InValue) override;

		TUniquePtr<IAudioInstanceTransmitter> Clone() const override;

		template<typename DataType>
		bool SendValue(const FName& InParameterName, const DataType& InValue)
		{
			if (TSender<DataType>* Sender = FindSenderOfDataType<DataType>(InParameterName))
			{
				return Sender->Push(InValue);
			}
			return false;
		}

	private:

		const FSendInfo* FindSendInfo(const FName& InParameterName) const;
		ISender* FindSender(const FName& InParameterName);

		template<typename DataType> 
		TSender<DataType>* FindSenderOfDataType(const FName& InParameterName)
		{
			using FSenderType = TSender<DataType>;

			if (ISender* ExistingSender = FindSender(InParameterName))
			{
				if (ExistingSender->CheckType<FSenderType>())
				{
					FSenderType& DerivedSender = ExistingSender->GetAs<FSenderType>();
					return &DerivedSender;
				}
				else
				{
					UE_LOG(LogMetasound, Warning, TEXT("Could not send to Metasound input. Mismatched data type [Recieved:%s, Expected: %s]"), *GetMetasoundDataTypeString<DataType>(), *ExistingSender->GetDataType().ToString());
				}
			}
			else if (const FSendInfo* SendInfo = FindSendInfo(InParameterName))
			{
				TUniquePtr<TSender<DataType>> NewSender = CreateSender<DataType>(*SendInfo);
				if (NewSender.IsValid())
				{
					FSenderType* DerivedSender = NewSender.Get();

					InputSends.Add(InParameterName, MoveTemp(NewSender));

					return DerivedSender;
				}
			}

			return nullptr;
		}

		template<typename DataType>
		TUniquePtr<TSender<DataType>> CreateSender(const FSendInfo& InInfo)
		{
			const float DelayTimeInSeconds = 10.0f; // TODO: likely want to remove this and opt for different protocols having different behaviors.
			FSenderInitParams InitParams = {OperatorSettings, DelayTimeInSeconds};

			if (InInfo.TypeName == GetMetasoundDataTypeName<DataType>())
			{
				return FDataTransmissionCenter::Get().RegisterNewSend<DataType>(InInfo.Address, InitParams);
			}
			else
			{
				UE_LOG(LogMetasound, Error, TEXT("Cannot create sender. Requested data type [TypeName:%s] does not match receive data type [TypeName:%s]"), *GetMetasoundDataTypeName<DataType>().ToString(), *InInfo.TypeName.ToString())
			}

			return TUniquePtr<TSender<DataType>>(nullptr);
		}


		TArray<FSendInfo> SendInfos;
		FOperatorSettings OperatorSettings;
		uint64 InstanceID;

		TMap<FName, TUniquePtr<ISender>> InputSends;
	};
}

