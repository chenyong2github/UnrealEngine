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

	uint64 FMetasoundInstanceTransmitter::GetInstanceID() const
	{
		return InstanceID;
	}

	bool FMetasoundInstanceTransmitter::SetFloatParameter(const FName& InParameterName, float InValue)
	{
		return SetParameterWithLiteral(InParameterName, FLiteral(InValue));
	}

	bool FMetasoundInstanceTransmitter::SetIntParameter(const FName& InParameterName, int32 InValue) 
	{
		return SetParameterWithLiteral(InParameterName, FLiteral(InValue));
	}

	bool FMetasoundInstanceTransmitter::SetBoolParameter(const FName& InParameterName, bool InValue) 
	{
		return SetParameterWithLiteral(InParameterName, FLiteral(InValue));
	}

	bool FMetasoundInstanceTransmitter::SetParameterWithLiteral(const FName& InParameterName, const FLiteral& InLiteral) 
	{
		ISender* Sender = FindSender(InParameterName);

		if (nullptr == Sender)
		{
			// If not sender exists for parameter name, attempt to add one.
			if (const FSendInfo* SendInfo = FindSendInfo(InParameterName))
			{
				Sender = AddSender(*SendInfo);
			}
		}

		if (nullptr != Sender)
		{
			// Push data using found sender.
			return Sender->PushLiteral(InLiteral);
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
		TUniquePtr<ISender>* SenderPtrPtr = InputSends.Find(InParameterName);
		if (SenderPtrPtr)
		{
			return SenderPtrPtr->Get();
		}
		return nullptr;
	}

	TUniquePtr<ISender> FMetasoundInstanceTransmitter::CreateSender(const FSendInfo& InInfo) const
	{
		// TODO: likely want to remove this and opt for different protocols having different behaviors.
		const float DelayTimeInSeconds = 0.1f; //< This not used for non-audio routing.
		FSenderInitParams InitParams = {OperatorSettings, DelayTimeInSeconds};

		return FDataTransmissionCenter::Get().RegisterNewSender(InInfo.TypeName, InInfo.Address, InitParams);
	}

	ISender* FMetasoundInstanceTransmitter::AddSender(const FSendInfo& InInfo)
	{
		TUniquePtr<ISender> Sender = CreateSender(InInfo);

		if (ensureMsgf(Sender.IsValid(), TEXT("Failed to create sender [DataType:%s, Address:%s]"), *InInfo.TypeName.ToString(), *InInfo.Address.ChannelName.ToString()))
		{
			ISender* Ptr = Sender.Get();
			InputSends.Add(InInfo.ParameterName, MoveTemp(Sender));
			return Ptr;
		}
		
		return nullptr;
	}
}
