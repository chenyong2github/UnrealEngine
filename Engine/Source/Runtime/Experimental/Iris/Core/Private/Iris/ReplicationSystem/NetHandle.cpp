// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/ReplicationSystem/NetHandle.h"
#include "Containers/UnrealString.h"
#include "Misc/StringBuilder.h"

namespace UE::Net
{

FString FNetHandle::ToString() const
{
	FString Result;
#if UE_NET_ALLOW_MULTIPLE_REPLICATION_SYSTEMS
	const uint32 ReplicationSystemIdToDisplay = ReplicationSystemId - 1U;
	Result = FString::Printf(TEXT("NetHandle (Id=%u):(RepSystemId=%u)"), GetId(), ReplicationSystemIdToDisplay);
#else
	Result = FString::Printf(TEXT("NetHandle (Id=%u)"), GetId());
#endif
	return Result;
}

}

FStringBuilderBase& operator<<(FStringBuilderBase& Builder, const UE::Net::FNetHandle& NetHandle) 
{ 	
#if UE_NET_ALLOW_MULTIPLE_REPLICATION_SYSTEMS
	const uint32 ReplicationSystemIdToDisplay = NetHandle.GetReplicationSystemId() - 1U;
	Builder.Appendf(TEXT("NetHandle (Id=%u):(RepSystemId=%u)"), NetHandle.GetId(), ReplicationSystemIdToDisplay);
#else
	Builder.Appendf(TEXT("NetHandle (Id=%u)"), NetHandle.GetId());
#endif
	return Builder;
}

FAnsiStringBuilderBase& operator<<(FAnsiStringBuilderBase& Builder, const UE::Net::FNetHandle& NetHandle)
{
	#if UE_NET_ALLOW_MULTIPLE_REPLICATION_SYSTEMS
		const uint32 ReplicationSystemIdToDisplay = NetHandle.GetReplicationSystemId() - 1U;
		Builder.Appendf("NetHandle (Id=%u):(RepSystemId=%u)", NetHandle.GetId(), ReplicationSystemIdToDisplay);
	#else
		Builder.Appendf("NetHandle (Id=%u)", NetHandle.GetId());
	#endif
		return Builder;
}
