// Copyright Epic Games, Inc. All Rights Reserved.
#include "Analysis/MetasoundFrontendAnalyzerAddress.h"


namespace Metasound
{
	namespace Frontend
	{
		const FString FAnalyzerAddress::PathSeparator = TEXT("/");

		FString FAnalyzerAddress::ToString() const
		{
			return FString::Join(TArray<FString>
			{
				*FString::Printf(TEXT("%lld"), InstanceID),
				*NodeID.ToString(),
				*OutputName.ToString(),
				*DataType.ToString(),
				*AnalyzerName.ToString(),
				*AnalyzerInstanceID.ToString(),
				*AnalyzerMemberName.ToString()
			}, *PathSeparator);
		}

		FSendAddress FAnalyzerAddress::ToSendAddress() const
		{
			const TArray<FString> ChannelTokens
			{
				NodeID.ToString(),
				OutputName.ToString(),
				AnalyzerName.ToString(),
				AnalyzerInstanceID.ToString(),
				AnalyzerMemberName.ToString()
			};

			// TODO: This is bad as its generating FNames like crazy. One idea was to include guid support in FSendAddresses to avoid FName generation,
			// which would enable this factory to map analyzer keys to send guid.
			const FName Channel = FName(*FString::Join(ChannelTokens, *PathSeparator));
			return FSendAddress { Channel, DataType, InstanceID };
		}

		bool FAnalyzerAddress::ParseKey(const FString& InAnalyzerKey, FAnalyzerAddress& OutAddress)
		{
			TArray<FString> Tokens;
			if (InAnalyzerKey.ParseIntoArray(Tokens, *PathSeparator) == 7)
			{
				OutAddress.InstanceID = static_cast<uint64>(FCString::Atoi64(*Tokens[0]));
				OutAddress.NodeID = FGuid(Tokens[1]);
				OutAddress.OutputName = *Tokens[2];
				OutAddress.DataType = *Tokens[3];
				OutAddress.AnalyzerName = *Tokens[4];
				OutAddress.AnalyzerInstanceID = FGuid(Tokens[5]);
				OutAddress.AnalyzerMemberName = *Tokens[6];

				return true;
			}

			return false;
		}
	} // namespace Frontend
} // namespace Metasound
