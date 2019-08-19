// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NetworkPredictionTypes.h"
#include "Engine/NetConnection.h"

DEFINE_LOG_CATEGORY(LogNetworkSim);

FColor FVisualLoggingParameters::DebugColors[(int32)EVisualLoggingContext::MAX] =
{
	FColor::Cyan,		// LastPredicted
	FColor::Cyan,		// OtherPredicted
	FColor::Green,		// LastConfirmed
	FColor::Red,		// FirstMispredicted
	FColor(255,81,101),	// OtherMispredicted
	FColor(255,91,91),	// LastMispredicted
	FColor::Purple,		// CurrentServerPIE
};

// -------------------------------------------------------------------------------------------------------------------------------
//	FReplicationProxy
// -------------------------------------------------------------------------------------------------------------------------------

void FReplicationProxy::Init(class IReplicationProxy* InNetworkSimModel, EReplicationProxyTarget InReplicationTarget)
{
	NetworkSimModel = InNetworkSimModel;
	ReplicationTarget = InReplicationTarget;
}

bool FReplicationProxy::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	NetworkSimModel->NetSerializeProxy(ReplicationTarget, Ar);
	return true;
}

void FReplicationProxy::OnPreReplication()
{
	CachedDirtyCount = NetworkSimModel->GetProxyDirtyCount(ReplicationTarget);
}
	
bool FReplicationProxy::Identical(const FReplicationProxy* Other, uint32 PortFlags) const
{
	return (CachedDirtyCount == Other->CachedDirtyCount);
}

// -------------------------------------------------------------------------------------------------------------------------------
//	FServerRPCProxyParameter
// -------------------------------------------------------------------------------------------------------------------------------

TArray<uint8> FServerReplicationRPCParameter::TempStorage;

bool FServerReplicationRPCParameter::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	if (Ar.IsLoading())
	{
		// Loading: serialize to temp storage. We'll do the real deserialize in a manual call to ::NetSerializeToProxy
		FNetBitReader& BitReader = (FNetBitReader&)Ar;
		CachedNumBits = BitReader.GetBitsLeft();
		CachedPackageMap = Map;

		const int64 BytesLeft = BitReader.GetBytesLeft();
		check(BytesLeft > 0); // Should not possibly be able to get here with an empty archive
		TempStorage.Reset(BytesLeft);
		TempStorage.SetNumUninitialized(BytesLeft);
		TempStorage.Last() = 0;

		BitReader.SerializeBits(TempStorage.GetData(), CachedNumBits);
	}
	else
	{
		// Saving: directly call into the proxy's NetSerialize. No need for temp storage.
		check(Proxy); // Must have been set before, via ctor.
		return Proxy->NetSerialize(Ar, Map, bOutSuccess);
	}

	return true;
}

void FServerReplicationRPCParameter::NetSerializeToProxy(FReplicationProxy& InProxy)
{
	check(CachedPackageMap != nullptr);
	check(CachedNumBits != -1);

	FNetBitReader BitReader(CachedPackageMap, TempStorage.GetData(), CachedNumBits);

	bool bOutSuccess = true;
	InProxy.NetSerialize(BitReader, CachedPackageMap, bOutSuccess);

	CachedNumBits = -1;
	CachedPackageMap = nullptr;
}

// -------------------------------------------------------------------------------------------------------------------------------
//	FScopedBandwidthLimitBypass
// -------------------------------------------------------------------------------------------------------------------------------

FScopedBandwidthLimitBypass::FScopedBandwidthLimitBypass(AActor* OwnerActor)
{
	if (OwnerActor)
	{
		CachedNetConnection = OwnerActor->GetNetConnection();
		if (CachedNetConnection)
		{
			RestoreBits = CachedNetConnection->QueuedBits + CachedNetConnection->SendBuffer.GetNumBits();
		}
	}
}

FScopedBandwidthLimitBypass::~FScopedBandwidthLimitBypass()
{
	if (CachedNetConnection)
	{
		CachedNetConnection->QueuedBits = RestoreBits - CachedNetConnection->SendBuffer.GetNumBits();
	}
}

FNetSimTickParametersBase::FNetSimTickParametersBase(AActor* OwningActor)
{
	// Default implementation that will probably be good for most actors
	Role = OwningActor->GetLocalRole();
	const bool bHasNetConnection = !!OwningActor->GetNetConnection();

	// The only default case we really don;t want generating local input is on the server when there is a net owning player (auto proxy)
	// This implies that we will (sim) extrapolate by default. To opt out of extrapolation, set to false or you can just not tick the net sim
	bGenerateLocalInputCmds = !(Role == ROLE_Authority && bHasNetConnection);
}