#include "Channel.h"

namespace Trace {

const FName FChannelProvider::ProviderName("ChannelProvider");

///////////////////////////////////////////////////////////////////////////////
FChannelProvider::FChannelProvider()
{
}

///////////////////////////////////////////////////////////////////////////////
void FChannelProvider::AnnounceChannel(const ANSICHAR* InChannelName, uint32 Id)
{
	FString ChannelName(ANSI_TO_TCHAR(InChannelName));
	ChannelName.GetCharArray()[0] = TChar<TCHAR>::ToUpper(ChannelName.GetCharArray()[0]);
	Channels.Add(FChannelEntry{
		Id,
		ChannelName,
		true
	});
}

///////////////////////////////////////////////////////////////////////////////
void FChannelProvider::UpdateChannel(uint32 Id, bool bEnabled)
{
	const auto FoundEntry = Channels.FindByPredicate([Id](const FChannelEntry& Entry) {
		return Entry.Id == Id;
	});

	if (FoundEntry)
	{
		FoundEntry->bIsEnabled = bEnabled;
	}
}

///////////////////////////////////////////////////////////////////////////////
uint64 FChannelProvider::GetChannelCount() const
{
	return Channels.Num();
}

///////////////////////////////////////////////////////////////////////////////
const TArray<FChannelEntry>& FChannelProvider::GetChannels() const
{
	return Channels;
}

}