// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NetworkPredictionTypes.h"
#include "Engine/NetConnection.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "VisualLogger/VisualLogger.h"

DEFINE_LOG_CATEGORY(LogNetworkSim);

namespace NetSimVLogCVars
{

static int32 UseVLogger = 1;
static FAutoConsoleVariableRef CVarUseVLogger(TEXT("ns.Debug.UseUnrealVLogger"),
	UseVLogger,	TEXT("Use Unreal Visual Logger\n"),	ECVF_Default);

static int32 UseDrawDebug = 1;
static FAutoConsoleVariableRef CVarUseDrawDebug(TEXT("ns.Debug.UseDrawDebug"),
	UseDrawDebug,	TEXT("Use built in DrawDebug* functions for visual logging\n"), ECVF_Default);

static float DrawDebugDefaultLifeTime = 20.f;
static FAutoConsoleVariableRef CVarDrawDebugDefaultLifeTime(TEXT("ns.Debug.DrawDebugLifetime.DefaultLifeTime"),
	DrawDebugDefaultLifeTime, TEXT("Use built in DrawDebug* functions for visual logging"), ECVF_Default);
};

FColor FVisualLoggingParameters::DebugColors[(int32)EVisualLoggingContext::MAX] =
{
	FColor::Cyan,		// LastPredicted
	FColor::Cyan,		// OtherPredicted
	FColor::Green,		// LastConfirmed
	FColor::Red,		// FirstMispredicted
	FColor(255,81,101),	// OtherMispredicted
	FColor(255,91,91),	// LastMispredicted
	FColor::Purple,		// CurrentServerPIE

	FColor(189, 195, 199),	// InterpolationBufferHead
	FColor(94, 96, 99),		// InterpolationBufferTail
	FColor(150, 150, 150),	// InterpolationFrom
	FColor(200, 200, 200),	// InterpolationTo

	FColor(0, 255, 0),	// InterpolationLatest
	FColor(255, 0, 0),	// InterpolationWaiting
	FColor(0, 0, 255),	// InterpolationSpeedUp
};

void FVisualLoggingHelpers::VisualLogActor(AActor* Owner, FTransform& Transform, const FVisualLoggingParameters& Params)
{
	const bool PersistentLines = (Params.Lifetime == EVisualLoggingLifetime::Persistent);
	const float LifetimeSeconds = PersistentLines ? NetSimVLogCVars::DrawDebugDefaultLifeTime : -1.f;
	const bool bDrawCumb = (Params.Context == EVisualLoggingContext::OtherMispredicted || Params.Context == EVisualLoggingContext::OtherPredicted);
	const FColor DrawColor = Params.GetDebugColor();

	UWorld* World = Owner->GetWorld();

	if (bDrawCumb)
	{
		if (NetSimVLogCVars::UseDrawDebug)
		{
			static float PointSize = 3.f;
			DrawDebugPoint(World, Transform.GetLocation(), PointSize, DrawColor, false, LifetimeSeconds);
		}

		if (NetSimVLogCVars::UseVLogger)
		{
			static float CrumbRadius = 3.f;
			UE_VLOG_LOCATION(Owner, LogNetworkSim, Log, Transform.GetLocation(), CrumbRadius, DrawColor, TEXT("%d"), Params.Frame);
		}
	}
	else
	{
		// Full drawing. Trying to provide a generic implementation that will give the most accurate representation.
		// Still, subclasses may want to customize this
		if (UCapsuleComponent* CapsuleComponent = Owner->FindComponentByClass<UCapsuleComponent>())
		{
			float Radius=0.f;
			float HalfHeight=0.f;
			CapsuleComponent->GetScaledCapsuleSize(Radius, HalfHeight);

			if (NetSimVLogCVars::UseDrawDebug)
			{
				static const float Thickness = 2.f;
				DrawDebugCapsule(World, Transform.GetLocation(), HalfHeight, Radius, Transform.GetRotation(), DrawColor, false, LifetimeSeconds, 0.f, Thickness);
			}

			if (NetSimVLogCVars::UseVLogger)
			{
				FVector VLogPosition = Transform.GetLocation();
				VLogPosition.Z -= HalfHeight;
				UE_VLOG_CAPSULE(Owner, LogNetworkSim, Log, VLogPosition, HalfHeight, Radius, Transform.GetRotation(), DrawColor, TEXT("%s"), *Params.DebugString);
			}
		}
		else
		{
			// Generic Actor Bounds drawing
			FBox LocalSpaceBox = Owner->CalculateComponentsBoundingBoxInLocalSpace();

			if (NetSimVLogCVars::UseDrawDebug)
			{
				static const float Thickness = 2.f;

				FVector ActorOrigin;
				FVector ActorExtent;
				LocalSpaceBox.GetCenterAndExtents(ActorOrigin, ActorExtent);
				ActorExtent *= Transform.GetScale3D();
				DrawDebugBox(World, Transform.GetLocation(), ActorExtent, Transform.GetRotation(), DrawColor, false, LifetimeSeconds, 0, Thickness);
			}

			if (NetSimVLogCVars::UseVLogger)
			{
				UE_VLOG_OBOX(Owner, LogNetworkSim, Log, LocalSpaceBox, Transform.ToMatrixWithScale(), DrawColor, TEXT("%s"), *Params.DebugString);
			}
		}
	}

	if (NetSimVLogCVars::UseVLogger)
	{
		UE_VLOG(Owner, LogNetworkSim, Log, TEXT("%s"), *Params.DebugString);
		UE_VLOG(Owner, LogNetworkSim, Log, TEXT("%s"), *Params.StateString);
	}
}

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

FNetSimTickParameters::FNetSimTickParameters(float InLocalDeltaTimeSeconds, AActor* OwningActor)
{
	LocalDeltaTimeSeconds = InLocalDeltaTimeSeconds;
	InitFromActor(OwningActor);
}

void FNetSimTickParameters::InitFromActor(AActor* Actor)
{
	// Default implementation that will probably be good for most actors
	Role = Actor->GetLocalRole();
	const bool bHasNetConnection = Actor->GetNetConnection() != nullptr;

	// The only default case we really don't want generating local input is on the server when there is a net owning player (auto proxy)
	// This implies that we will (sim) extrapolate by default. To opt out of extrapolation, set to false or you can just not tick the net sim
	bGenerateLocalInputCmds = !(Role == ROLE_Authority && bHasNetConnection);
}