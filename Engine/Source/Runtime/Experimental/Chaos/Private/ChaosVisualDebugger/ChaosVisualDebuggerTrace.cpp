// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVisualDebugger/ChaosVisualDebuggerTrace.h"

#if CHAOS_VISUAL_DEBUGGER_ENABLED
#include "Chaos/ParticleHandle.h"
#include "Chaos/Framework/PhysicsSolverBase.h"
#include "Chaos/ImplicitObject.h"
#include "ChaosVDRuntimeModule.h"
#include "Compression/OodleDataCompressionUtil.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeRWLock.h"
#include "Serialization/MemoryWriter.h"

UE_TRACE_EVENT_DEFINE(ChaosVDLogger, ChaosVDSolverFrameStart)

UE_TRACE_EVENT_DEFINE(ChaosVDLogger, ChaosVDSolverFrameEnd)

UE_TRACE_CHANNEL_DEFINE(ChaosVDChannel);
UE_TRACE_EVENT_DEFINE(ChaosVDLogger, ChaosVDParticle)
UE_TRACE_EVENT_DEFINE(ChaosVDLogger, ChaosVDSolverStepStart)
UE_TRACE_EVENT_DEFINE(ChaosVDLogger, ChaosVDSolverStepEnd)
UE_TRACE_EVENT_DEFINE(ChaosVDLogger, ChaosVDBinaryDataStart)
UE_TRACE_EVENT_DEFINE(ChaosVDLogger, ChaosVDBinaryDataContent)
UE_TRACE_EVENT_DEFINE(ChaosVDLogger, ChaosVDBinaryDataEnd)
UE_TRACE_EVENT_DEFINE(ChaosVDLogger, ChaosVDSolverSimulationSpace)

static FAutoConsoleVariable CVarChaosVDCompressBinaryData(
	TEXT("p.Chaos.VD.CompressBinaryData"),
	false,
	TEXT("If true, serialized binary data will be compressed using Oodle on the fly before being traced"));

static FAutoConsoleVariable CVarChaosVDCompressionMode(
	TEXT("p.Chaos.VD.CompressionMode"),
	2,
	TEXT("Oodle compression mode to use, 4 is by default which equsals to ECompressionLevel::VeryFast"));

struct FChaosVDGeometryTraceContext
{
	FRWLock RWLock;
	TUniquePtr<Chaos::FChaosArchiveContext> ChaosContext;
};

static FChaosVDGeometryTraceContext GeometryTracerObject = FChaosVDGeometryTraceContext();

void FChaosVisualDebuggerTrace::TraceParticle(const Chaos::FGeometryParticleHandle* ParticleHandle)
{
	if (!UE_TRACE_CHANNELEXPR_IS_ENABLED(ChaosVDChannel))
	{
		return;
	}

	const FChaosVDContext* CVDContextData = FChaosVDThreadContext::Get().GetCurrentContext();
	if (!ensure(CVDContextData))
	{
		return;
	}

	TraceParticle(const_cast<Chaos::FGeometryParticleHandle*>(ParticleHandle), *CVDContextData);
}

void FChaosVisualDebuggerTrace::TraceParticle(Chaos::FGeometryParticleHandle* ParticleHandle, const FChaosVDContext& ContextData)
{
	if (!UE_TRACE_CHANNELEXPR_IS_ENABLED(ChaosVDChannel))
	{
		return;
	}

	if (!ParticleHandle)
	{
		UE_LOG(LogChaos, Warning, TEXT("Tried to Trace a null particle %hs"), __FUNCTION__);
		return;
	}

	static FString DefaultName = TEXT("NONAME");
	FStringView ParticleNameView(DefaultName);

#if CHAOS_DEBUG_NAME
	if (const TSharedPtr<FString, ESPMode::ThreadSafe>& DebugNamePtr = ParticleHandle->DebugName())
	{
		ParticleNameView = FStringView(*DebugNamePtr.Get());
	}
#endif
	const int32 GeometryID = TraceImplicitObject(ParticleHandle->Geometry());
	
	UE_TRACE_LOG(ChaosVDLogger, ChaosVDParticle, ChaosVDChannel)
		<< ChaosVDParticle.SolverID(ContextData.Id)
		<< ChaosVDParticle.Cycle(FPlatformTime::Cycles64())

		<< ChaosVDParticle.ParticleID(ParticleHandle->UniqueIdx().Idx)
		<< ChaosVDParticle.ParticleType(static_cast<uint8>(ParticleHandle->Type))
		<< ChaosVDParticle.DebugName(ParticleNameView.GetData(), ParticleNameView.Len())

		<< CVD_TRACE_VECTOR_ON_EVENT(ChaosVDParticle, Position, ParticleHandle->X())
		<< CVD_TRACE_ROTATOR_ON_EVENT(ChaosVDParticle, Rotation, ParticleHandle->R())
		<< CVD_TRACE_VECTOR_ON_EVENT(ChaosVDParticle, Velocity, Chaos::FConstGenericParticleHandle(ParticleHandle)->V())
		<< CVD_TRACE_VECTOR_ON_EVENT(ChaosVDParticle, AngularVelocity, Chaos::FConstGenericParticleHandle(ParticleHandle)->W())

		<< ChaosVDParticle.ImplicitObjectID(GeometryID)

		<< ChaosVDParticle.ObjectState(static_cast<int8>(ParticleHandle->ObjectState()));
	
}

void FChaosVisualDebuggerTrace::TraceParticles(const Chaos::TGeometryParticleHandles<Chaos::FReal, 3>& ParticleHandles)
{
	if (!UE_TRACE_CHANNELEXPR_IS_ENABLED(ChaosVDChannel))
	{
		return;
	}

	const FChaosVDContext* CVDContextData = FChaosVDThreadContext::Get().GetCurrentContext();
	if (!ensure(CVDContextData))
	{
		return;
	}

	for (uint32 ParticleIndex = 0; ParticleIndex < ParticleHandles.Size(); ParticleIndex++)
	{
		// TODO: We should only trace the Particles that changed. probably using the Dirty Flags?
		// Geometry data "uniqueness" will be handled by the trace helper
		TraceParticle(ParticleHandles.Handle(ParticleIndex).Get(), *CVDContextData);
	}
}

void FChaosVisualDebuggerTrace::TraceSolverFrameStart(const FChaosVDContext& ContextData, const FString& InDebugName)
{
	if (!ensure(ContextData.Id != INDEX_NONE))
	{
		return;
	}

	FChaosVDThreadContext::Get().PushContext(ContextData);

	UE_TRACE_LOG(ChaosVDLogger, ChaosVDSolverFrameStart, ChaosVDChannel)
		<< ChaosVDSolverFrameStart.SolverID(ContextData.Id)
		<< ChaosVDSolverFrameStart.Cycle(FPlatformTime::Cycles64())
		<< ChaosVDSolverFrameStart.DebugName(*InDebugName, InDebugName.Len());
}

void FChaosVisualDebuggerTrace::TraceSolverFrameEnd(const FChaosVDContext& ContextData)
{
	FChaosVDThreadContext::Get().PopContext();

	if (!ensure(ContextData.Id != INDEX_NONE))
	{
		return;
	}

	UE_TRACE_LOG(ChaosVDLogger, ChaosVDSolverFrameEnd, ChaosVDChannel)
		<< ChaosVDSolverFrameEnd.SolverID(ContextData.Id)
		<< ChaosVDSolverFrameEnd.Cycle(FPlatformTime::Cycles64());
}

void FChaosVisualDebuggerTrace::TraceSolverStepStart()
{
	if (!UE_TRACE_CHANNELEXPR_IS_ENABLED(ChaosVDChannel))
	{
		return;
	}

	const FChaosVDContext* CVDContextData = FChaosVDThreadContext::Get().GetCurrentContext();
	if (!ensure(CVDContextData))
	{
		return;
	}

	UE_TRACE_LOG(ChaosVDLogger, ChaosVDSolverStepStart, ChaosVDChannel)
		<< ChaosVDSolverStepStart.Cycle(FPlatformTime::Cycles64())
		<< ChaosVDSolverStepStart.SolverID(CVDContextData->Id);
}

void FChaosVisualDebuggerTrace::TraceSolverStepEnd()
{
	if (!UE_TRACE_CHANNELEXPR_IS_ENABLED(ChaosVDChannel))
	{
		return;
	}

	const FChaosVDContext* CVDContextData = FChaosVDThreadContext::Get().GetCurrentContext();
	if (!ensure(CVDContextData))
	{
		return;
	}

	UE_TRACE_LOG(ChaosVDLogger, ChaosVDSolverStepEnd, ChaosVDChannel)
		<< ChaosVDSolverStepEnd.Cycle(FPlatformTime::Cycles64())
		<< ChaosVDSolverStepEnd.SolverID(CVDContextData->Id);
}

void FChaosVisualDebuggerTrace::TraceSolverSimulationSpace(const Chaos::FRigidTransform3& Transform)
{
	if (!UE_TRACE_CHANNELEXPR_IS_ENABLED(ChaosVDChannel))
	{
		return;
	}

	const FChaosVDContext* CVDContextData = FChaosVDThreadContext::Get().GetCurrentContext();
	if (!ensure(CVDContextData))
	{
		return;
	}
	
	UE_TRACE_LOG(ChaosVDLogger, ChaosVDSolverSimulationSpace, ChaosVDChannel)
		<< ChaosVDSolverSimulationSpace.Cycle(FPlatformTime::Cycles64())
		<< ChaosVDSolverSimulationSpace.SolverID(CVDContextData->Id)
		<< CVD_TRACE_VECTOR_ON_EVENT(ChaosVDSolverSimulationSpace, Position, Transform.GetLocation())
		<< CVD_TRACE_ROTATOR_ON_EVENT(ChaosVDSolverSimulationSpace, Rotation, Transform.GetRotation());
}

void FChaosVisualDebuggerTrace::TraceBinaryData(const TArray<uint8>& InData, const FString& TypeName)
{
	if (!UE_TRACE_CHANNELEXPR_IS_ENABLED(ChaosVDChannel))
	{
		return;
	}

	//TODO: This might overflow
	static FThreadSafeCounter LastDataID;

	const int32 DataID = LastDataID.Increment();

	ensure(DataID < TNumericLimits<int32>::Max());

	const TArray<uint8>* DataToTrace = &InData;

	// Handle Compression if enabled
	const bool bIsCompressed = CVarChaosVDCompressBinaryData->GetBool();
	TArray<uint8> CompressedData;
	if (bIsCompressed)
	{
		CompressedData.Reserve(CompressedData.Num());
		FOodleCompressedArray::CompressTArray(CompressedData, InData, FOodleDataCompression::ECompressor::Kraken,
			static_cast<FOodleDataCompression::ECompressionLevel>(CVarChaosVDCompressionMode->GetInt()));

		DataToTrace = &CompressedData;
	}

	const uint32 DataSize = static_cast<uint32>(DataToTrace->Num());
	constexpr uint32 MaxChunkSize = TNumericLimits<uint16>::Max();
	const uint32 ChunkNum = (DataSize + MaxChunkSize - 1) / MaxChunkSize;

	UE_TRACE_LOG(ChaosVDLogger, ChaosVDBinaryDataStart, ChaosVDChannel)
		<< ChaosVDBinaryDataStart.Cycle(FPlatformTime::Cycles64())
		<< ChaosVDBinaryDataStart.TypeName(*TypeName, TypeName.Len())
		<< ChaosVDBinaryDataStart.DataID(DataID)
		<< ChaosVDBinaryDataStart.DataSize(DataSize)
		<< ChaosVDBinaryDataStart.OriginalSize(InData.Num())
		<< ChaosVDBinaryDataStart.IsCompressed(bIsCompressed);

	uint32 RemainingSize = DataSize;
	for (uint32 Index = 0; Index < ChunkNum; ++Index)
	{
		const uint16 Size = static_cast<uint16>(FMath::Min(RemainingSize, MaxChunkSize));
		const uint8* ChunkData = DataToTrace->GetData() + MaxChunkSize * Index;

		UE_TRACE_LOG(ChaosVDLogger, ChaosVDBinaryDataContent, ChaosVDChannel)
			<< ChaosVDBinaryDataContent.Cycle(FPlatformTime::Cycles64())
			<< ChaosVDBinaryDataContent.DataID(DataID)
			<< ChaosVDBinaryDataContent.RawData(ChunkData, Size);

		RemainingSize -= Size;
	}

	UE_TRACE_LOG(ChaosVDLogger, ChaosVDBinaryDataEnd, ChaosVDChannel)
		<< ChaosVDBinaryDataEnd.Cycle(FPlatformTime::Cycles64())
		<< ChaosVDBinaryDataEnd.DataID(DataID);

	ensure(RemainingSize == 0);
}

int32 FChaosVisualDebuggerTrace::TraceImplicitObject(Chaos::TSerializablePtr<Chaos::FImplicitObject> Geometry)
{
	if (!UE_TRACE_CHANNELEXPR_IS_ENABLED(ChaosVDChannel))
	{
		return INDEX_NONE;
	}

	//TODO: Find a better place to do this
	if (!FChaosVDRuntimeModule::OnRecordingStop().IsBound())
	{
		FChaosVDRuntimeModule::OnRecordingStop().BindStatic(&FChaosVisualDebuggerTrace::ResetGeometryTracerContext);
	}
	
	if (!FChaosVDRuntimeModule::OnRecordingStarted().IsBound())
	{
		FChaosVDRuntimeModule::OnRecordingStarted().BindStatic(&FChaosVisualDebuggerTrace::ResetGeometryTracerContext);
	}

	{
		FReadScopeLock Lock(GeometryTracerObject.RWLock);
		if (GeometryTracerObject.ChaosContext.IsValid())
		{
			int32 SerializedObjectPtrTag = GeometryTracerObject.ChaosContext.Get()->GetObjectTag(Geometry.Get());
			if (SerializedObjectPtrTag != INDEX_NONE)
			{
				// Geometry Data is already serialized, just return the tag
				return SerializedObjectPtrTag;
			}
		}
	}

	// TODO: Change this so it is not allocated each time
	// We need to take into account we could be serializing on multiple threads
	constexpr uint32 MaxTraceChunkSize = TNumericLimits<uint16>::Max();
	TArray<uint8> RawData;
	RawData.Reserve(MaxTraceChunkSize);
	
	int32 SerializedObjectPtrTag = INDEX_NONE;

	{
		FMemoryWriter MemWriterAr(RawData);
		Chaos::FChaosArchive Ar(MemWriterAr);
		
		FWriteScopeLock WriteLock(GeometryTracerObject.RWLock);
		if (GeometryTracerObject.ChaosContext.IsValid())
		{
			Ar.SetContext(MoveTemp(GeometryTracerObject.ChaosContext));
		}

		Ar << Geometry;
		GeometryTracerObject.ChaosContext = Ar.StealContext();
		SerializedObjectPtrTag = GeometryTracerObject.ChaosContext.Get()->GetObjectTag(Geometry.Get());
	}

	TraceBinaryData(RawData, TEXT("FImplicitObject"));

	return SerializedObjectPtrTag;
}

void FChaosVisualDebuggerTrace::ResetGeometryTracerContext()
{
	GeometryTracerObject.ChaosContext.Release();
}

#endif
