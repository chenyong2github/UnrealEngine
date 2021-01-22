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
		return SendValue<float>(InParameterName, InValue);
	}

	bool FMetasoundInstanceTransmitter::SetIntParameter(const FName& InParameterName, int32 InValue) 
	{
		return SendValue<int32>(InParameterName, InValue);
	}

	bool FMetasoundInstanceTransmitter::SetBoolParameter(const FName& InParameterName, bool InValue) 
	{
		return SendValue<bool>(InParameterName, InValue);
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
}
