// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundInstanceTransmitter.h"

namespace Metasound
{
	FMetasoundInstanceTransmitter::FMetasoundInstanceTransmitter(const FMetasoundInstanceTransmitter::FInitParams& InInitParams)
	: SendInfos(InInitParams.Infos)
	, OperatorSettings(InInitParams.OperatorSettings)
	, InstanceID(InInitParams.InstanceID)
	{
	}

	bool FMetasoundInstanceTransmitter::Shutdown()
	{
		bool bSuccess = true;

		for (const FSendInfo& SendInfo : SendInfos)
		{
			bSuccess &= FDataTransmissionCenter::Get().UnregisterDataChannel(SendInfo.TypeName, SendInfo.Address);
		}

		return bSuccess;
	}

	uint64 FMetasoundInstanceTransmitter::GetInstanceID() const
	{
		return InstanceID;
	}

	bool FMetasoundInstanceTransmitter::SetParameter(const FName& InParameterName, float InValue)
	{
		return SetParameterWithLiteral(InParameterName, FLiteral(InValue));
	}

	bool FMetasoundInstanceTransmitter::SetParameter(const FName& InParameterName, int32 InValue) 
	{
		return SetParameterWithLiteral(InParameterName, FLiteral(InValue));
	}

	bool FMetasoundInstanceTransmitter::SetParameter(const FName& InParameterName, bool InValue) 
	{
		return SetParameterWithLiteral(InParameterName, FLiteral(InValue));
	}

	bool FMetasoundInstanceTransmitter::SetParameter(const FName& InParameterName, TArray<bool>&& InValue)
	{
		return SetParameterWithLiteral(InParameterName, FLiteral(MoveTemp(InValue)));
	}

	bool FMetasoundInstanceTransmitter::SetParameter(const FName& InParameterName, TArray<int32>&& InValue)
	{
		return SetParameterWithLiteral(InParameterName, FLiteral(MoveTemp(InValue)));
	}

	bool FMetasoundInstanceTransmitter::SetParameter(const FName& InParameterName, TArray<float>&& InValue)
	{
		return SetParameterWithLiteral(InParameterName, FLiteral(MoveTemp(InValue)));
	}

	bool FMetasoundInstanceTransmitter::SetParameter(const FName& InParameterName, FString&& InValue)
	{
		return SetParameterWithLiteral(InParameterName, FLiteral(MoveTemp(InValue)));
	}

	bool FMetasoundInstanceTransmitter::SetParameter(const FName& InParameterName, TArray<FString>&& InValue)
	{
		return SetParameterWithLiteral(InParameterName, FLiteral(MoveTemp(InValue)));
	}

	bool FMetasoundInstanceTransmitter::SetParameter(const FName& InParameterName, Audio::IProxyDataPtr&& InValue)
	{
		return SetParameterWithLiteral(InParameterName, FLiteral(MoveTemp(InValue)));
	}

	bool FMetasoundInstanceTransmitter::SetParameter(const FName& InParameterName, TArray<Audio::IProxyDataPtr>&& InValue)
	{
		return SetParameterWithLiteral(InParameterName, FLiteral(MoveTemp(InValue)));
	}

	bool FMetasoundInstanceTransmitter::SetParameterWithLiteral(const FName& InParameterName, const FLiteral& InLiteral)
	{
		if (ISender* Sender = FindSender(InParameterName))
		{
			return Sender->PushLiteral(InLiteral);
		}

		// If no sender exists for parameter name, attempt to add one.
		if (const FSendInfo* SendInfo = FindSendInfo(InParameterName))
		{
			if (ISender* Sender = AddSender(*SendInfo))
			{
				return Sender->PushLiteral(InLiteral);
			}
		}

		return false;
	}

	TUniquePtr<IAudioInstanceTransmitter> FMetasoundInstanceTransmitter::Clone() const
	{
		return MakeUnique<FMetasoundInstanceTransmitter>(FMetasoundInstanceTransmitter::FInitParams(OperatorSettings, InstanceID, SendInfos));
	}

	const FMetasoundInstanceTransmitter::FSendInfo* FMetasoundInstanceTransmitter::FindSendInfo(const FName& InParameterName) const
	{
		return SendInfos.FindByPredicate([&](const FSendInfo& Info) { return Info.ParameterName == InParameterName; });
	}

	ISender* FMetasoundInstanceTransmitter::FindSender(const FName& InParameterName)
	{
		if (TUniquePtr<ISender>* SenderPtrPtr = InputSends.Find(InParameterName))
		{
			return SenderPtrPtr->Get();
		}
		return nullptr;
	}

	ISender* FMetasoundInstanceTransmitter::AddSender(const FSendInfo& InInfo)
	{
		// TODO: likely want to remove this and opt for different protocols having different behaviors.
		const float DelayTimeInSeconds = 0.1f; // This not used for non-audio routing.
		const FSenderInitParams InitParams = { OperatorSettings, DelayTimeInSeconds };

		TUniquePtr<ISender> Sender = FDataTransmissionCenter::Get().RegisterNewSender(InInfo.TypeName, InInfo.Address, InitParams);
		if (ensureMsgf(Sender.IsValid(), TEXT("Failed to create sender [DataType:%s, Address:%s]"), *InInfo.TypeName.ToString(), *InInfo.Address.ChannelName.ToString()))
		{
			ISender* Ptr = Sender.Get();
			InputSends.Add(InInfo.ParameterName, MoveTemp(Sender));
			return Ptr;
		}

		return nullptr;
	}
}
