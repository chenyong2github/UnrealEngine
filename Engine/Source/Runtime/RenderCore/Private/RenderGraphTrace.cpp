// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraphTrace.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphPrivate.h"
#include "Trace/Trace.inl"

#if RDG_ENABLE_TRACE

UE_TRACE_CHANNEL_DEFINE(RDGChannel)

UE_TRACE_EVENT_BEGIN(RDGTrace, GraphMessage)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, Name)
	UE_TRACE_EVENT_FIELD(uint64, StartCycles)
	UE_TRACE_EVENT_FIELD(uint64, EndCycles)
	UE_TRACE_EVENT_FIELD(uint16, PassCount)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(RDGTrace, GraphEndMessage)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(RDGTrace, PassMessage)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, Name)
	UE_TRACE_EVENT_FIELD(uint64, StartCycles)
	UE_TRACE_EVENT_FIELD(uint64, EndCycles)
	UE_TRACE_EVENT_FIELD(uint16, Handle)
	UE_TRACE_EVENT_FIELD(uint16, GraphicsForkPass)
	UE_TRACE_EVENT_FIELD(uint16, GraphicsJoinPass)
	UE_TRACE_EVENT_FIELD(uint16[], Textures)
	UE_TRACE_EVENT_FIELD(uint16[], Buffers)
	UE_TRACE_EVENT_FIELD(uint16, Flags)
	UE_TRACE_EVENT_FIELD(uint16, Pipeline)
	UE_TRACE_EVENT_FIELD(bool, IsCulled)
	UE_TRACE_EVENT_FIELD(bool, IsAsyncComputeBegin)
	UE_TRACE_EVENT_FIELD(bool, IsAsyncComputeEnd)
	UE_TRACE_EVENT_FIELD(bool, SkipRenderPassBegin)
	UE_TRACE_EVENT_FIELD(bool, SkipRenderPassEnd)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(RDGTrace, BufferMessage)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, Name)
	UE_TRACE_EVENT_FIELD(uint32, UsageFlags)
	UE_TRACE_EVENT_FIELD(uint32, BytesPerElement)
	UE_TRACE_EVENT_FIELD(uint32, NumElements)
	UE_TRACE_EVENT_FIELD(uint16, Handle)
	UE_TRACE_EVENT_FIELD(uint16, NextOwnerHandle)
	UE_TRACE_EVENT_FIELD(uint16, Order)
	UE_TRACE_EVENT_FIELD(uint16[], Passes)
	UE_TRACE_EVENT_FIELD(bool, IsExternal)
	UE_TRACE_EVENT_FIELD(bool, IsExtracted)
	UE_TRACE_EVENT_FIELD(bool, IsCulled)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(RDGTrace, TextureMessage)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, Name)
	UE_TRACE_EVENT_FIELD(uint64, StartCycles)
	UE_TRACE_EVENT_FIELD(uint64, EndCycles)
	UE_TRACE_EVENT_FIELD(uint16, Handle)
	UE_TRACE_EVENT_FIELD(uint16, NextOwnerHandle)
	UE_TRACE_EVENT_FIELD(uint16, Order)
	UE_TRACE_EVENT_FIELD(uint16[], Passes)
	UE_TRACE_EVENT_FIELD(uint64, SizeInBytes)
	UE_TRACE_EVENT_FIELD(uint64, CreateFlags)
	UE_TRACE_EVENT_FIELD(uint32, Dimension)
	UE_TRACE_EVENT_FIELD(uint32, Format)
	UE_TRACE_EVENT_FIELD(uint32, ExtentX)
	UE_TRACE_EVENT_FIELD(uint32, ExtentY)
	UE_TRACE_EVENT_FIELD(uint16, Depth)
	UE_TRACE_EVENT_FIELD(uint16, ArraySize)
	UE_TRACE_EVENT_FIELD(uint8, NumMips)
	UE_TRACE_EVENT_FIELD(uint8, NumSamples)
	UE_TRACE_EVENT_FIELD(bool, IsExternal)
	UE_TRACE_EVENT_FIELD(bool, IsExtracted)
	UE_TRACE_EVENT_FIELD(bool, IsCulled)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(RDGTrace, ScopeMessage)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, Name)
	UE_TRACE_EVENT_FIELD(uint16, FirstPass)
	UE_TRACE_EVENT_FIELD(uint16, LastPass)
	UE_TRACE_EVENT_FIELD(uint16, Depth)
UE_TRACE_EVENT_END()

static_assert(sizeof(FRDGPassHandle) == sizeof(uint16), "Expected 16 bit pass handles.");
static_assert(sizeof(FRDGTextureHandle) == sizeof(uint16), "Expected 16 bit texture handles.");
static_assert(sizeof(FRDGBufferHandle) == sizeof(uint16), "Expected 16 bit buffer handles.");

bool IsTraceEnabled()
{
	return UE_TRACE_CHANNELEXPR_IS_ENABLED(RDGChannel) && !GRDGImmediateMode;
}

void FRDGTrace::OutputGraphBegin()
{
	if (!IsTraceEnabled())
	{
		return;
	}

	GraphStartCycles = FPlatformTime::Cycles64();
}

void FRDGTrace::OutputGraphEnd(const FRDGBuilder& GraphBuilder)
{
	if (!IsTraceEnabled())
	{
		return;
	}

	const FRDGPassHandle ProloguePassHandle = GraphBuilder.GetProloguePassHandle();

	const auto& Passes = GraphBuilder.Passes;
	const auto& Textures = GraphBuilder.Textures;
	const auto& Buffers = GraphBuilder.Buffers;

	{
		const TCHAR* Name = GraphBuilder.BuilderName.GetTCHAR();

		UE_TRACE_LOG(RDGTrace, GraphMessage, RDGChannel)
			<< GraphMessage.Name(Name, uint16(FCString::Strlen(Name)))
			<< GraphMessage.StartCycles(GraphStartCycles)
			<< GraphMessage.EndCycles(FPlatformTime::Cycles64())
			<< GraphMessage.PassCount(uint16(Passes.Num()));
	}

	for (FRDGPassHandle Handle = Passes.Begin(); Handle != Passes.End(); ++Handle)
	{
		const FRDGPass* Pass = Passes[Handle];
		const TCHAR* Name = Pass->GetEventName().GetTCHAR();

		UE_TRACE_LOG(RDGTrace, PassMessage, RDGChannel)
			<< PassMessage.Name(Name, uint16(FCString::Strlen(Name)))
			<< PassMessage.Handle(Handle.GetIndex())
			<< PassMessage.GraphicsForkPass(Pass->GetGraphicsForkPass().GetIndexUnchecked())
			<< PassMessage.GraphicsJoinPass(Pass->GetGraphicsJoinPass().GetIndexUnchecked())
			<< PassMessage.Textures((const uint16*)Pass->TraceTextures.GetData(), (uint16)Pass->TraceTextures.Num())
			<< PassMessage.Buffers((const uint16*)Pass->TraceBuffers.GetData(), (uint16)Pass->TraceBuffers.Num())
			<< PassMessage.Flags(uint16(Pass->GetFlags()))
			<< PassMessage.Pipeline(uint16(Pass->GetPipeline()))
			<< PassMessage.IsCulled(GraphBuilder.PassesToCull[Handle])
			<< PassMessage.IsAsyncComputeBegin(Pass->bAsyncComputeBegin != 0)
			<< PassMessage.IsAsyncComputeEnd(Pass->bAsyncComputeEnd != 0)
			<< PassMessage.SkipRenderPassBegin(Pass->bSkipRenderPassBegin != 0)
			<< PassMessage.SkipRenderPassEnd(Pass->bSkipRenderPassEnd != 0);
	}

#if RDG_EVENTS
	{
		struct FScopeInfo
		{
			const TCHAR* Name{};
			FRDGPassHandle FirstPass;
			FRDGPassHandle LastPass;
			uint32 Depth{};
		};
		TArray<FScopeInfo> Scopes;
		TMap<const FRDGEventScope*, int32> ScopeToIndex;
		int32 Depth = 0;

		TRDGScopeStackHelper<FRDGEventScope> ScopeStackHelper;

		for (FRDGPassHandle Handle = Passes.Begin(); Handle != Passes.End(); ++Handle)
		{
			const FRDGPass* Pass = Passes[Handle];

			const FRDGEventScope* ParentScope = Pass->TraceEventScope;

			const auto PushScope = [&](const FRDGEventScope* ScopeToPush)
			{
				ScopeToIndex.Emplace(ScopeToPush, Scopes.Num());

				FScopeInfo ScopeInfo;
				ScopeInfo.Name = ScopeToPush->Name.GetTCHAR();
				ScopeInfo.FirstPass = Handle;
				ScopeInfo.Depth = Depth;
				Scopes.Add(ScopeInfo);

				Depth++;
			};

			const auto PopScope = [&](const FRDGEventScope* ScopeToPop)
			{
				FScopeInfo& ScopeInfo = Scopes[ScopeToIndex.FindChecked(ScopeToPop)];
				ScopeInfo.LastPass = FRDGPassHandle(Handle.GetIndex() - 1);

				Depth--;
			};

			ScopeStackHelper.BeginExecutePass(ParentScope, PushScope, PopScope);

			if (Handle == Passes.Last())
			{
				ScopeStackHelper.EndExecute(PopScope);
			}
		}

		check(Depth == 0);

		for (const FScopeInfo& ScopeInfo : Scopes)
		{
			UE_TRACE_LOG(RDGTrace, ScopeMessage, RDGChannel)
				<< ScopeMessage.Name(ScopeInfo.Name, uint16(FCString::Strlen(ScopeInfo.Name)))
				<< ScopeMessage.FirstPass(ScopeInfo.FirstPass.GetIndexUnchecked())
				<< ScopeMessage.LastPass(ScopeInfo.LastPass.GetIndexUnchecked())
				<< ScopeMessage.Depth(ScopeInfo.Depth);
		}
	}
#endif

	for (FRDGTextureHandle Handle = Textures.Begin(); Handle != Textures.End(); ++Handle)
	{
		const FRDGTexture* Texture = Textures[Handle];

		uint64 SizeInBytes = 0;
		if (FRHITexture* TextureRHI = Texture->GetRHIUnchecked())
		{
			SizeInBytes = RHIComputeMemorySize(TextureRHI);
		}

		UE_TRACE_LOG(RDGTrace, TextureMessage, RDGChannel)
			<< TextureMessage.Name(Texture->Name, uint16(FCString::Strlen(Texture->Name)))
			<< TextureMessage.Handle(Handle.GetIndex())
			<< TextureMessage.NextOwnerHandle(Texture->NextOwner.GetIndexUnchecked())
			<< TextureMessage.Order(Texture->TraceOrder)
			<< TextureMessage.Passes((const uint16*)Texture->TracePasses.GetData(), (uint16)Texture->TracePasses.Num())
			<< TextureMessage.SizeInBytes(SizeInBytes)
			<< TextureMessage.CreateFlags(uint32(Texture->Desc.Flags))
			<< TextureMessage.Dimension(uint32(Texture->Desc.Dimension))
			<< TextureMessage.Format(uint32(Texture->Desc.Format))
			<< TextureMessage.ExtentX(Texture->Desc.Extent.X)
			<< TextureMessage.ExtentY(Texture->Desc.Extent.Y)
			<< TextureMessage.Depth(Texture->Desc.Depth)
			<< TextureMessage.ArraySize(Texture->Desc.ArraySize)
			<< TextureMessage.NumMips(Texture->Desc.NumMips)
			<< TextureMessage.NumSamples(Texture->Desc.NumSamples)
			<< TextureMessage.IsExternal(bool(Texture->bExternal))
			<< TextureMessage.IsExtracted(bool(Texture->bExtracted))
			<< TextureMessage.IsCulled(bool(Texture->bCulled));
	}

	for (FRDGBufferHandle Handle = Buffers.Begin(); Handle != Buffers.End(); ++Handle)
	{
		const FRDGBuffer* Buffer = Buffers[Handle];

		UE_TRACE_LOG(RDGTrace, BufferMessage, RDGChannel)
			<< BufferMessage.Name(Buffer->Name, uint16(FCString::Strlen(Buffer->Name)))
			<< BufferMessage.Handle(Buffer->Handle.GetIndex())
			<< BufferMessage.NextOwnerHandle(Buffer->NextOwner.GetIndexUnchecked())
			<< BufferMessage.Order(Buffer->TraceOrder)
			<< BufferMessage.Passes((const uint16*)Buffer->TracePasses.GetData(), (uint16)Buffer->TracePasses.Num())
			<< BufferMessage.UsageFlags(uint32(Buffer->Desc.Usage))
			<< BufferMessage.BytesPerElement(Buffer->Desc.BytesPerElement)
			<< BufferMessage.NumElements(Buffer->Desc.NumElements)
			<< BufferMessage.IsExternal(bool(Buffer->bExternal))
			<< BufferMessage.IsExtracted(bool(Buffer->bExtracted))
			<< BufferMessage.IsCulled(bool(Buffer->bCulled));
	}

	UE_TRACE_LOG(RDGTrace, GraphEndMessage, RDGChannel);
}

void FRDGTrace::AddResource(FRDGParentResource* Resource)
{
	Resource->TraceOrder = ResourceOrder++;
}

void FRDGTrace::AddTexturePassDependency(FRDGTexture* Texture, FRDGPass* Pass)
{
	Pass->TraceTextures.Add(Texture->Handle);
	Texture->TracePasses.Add(Pass->Handle);
}

void FRDGTrace::AddBufferPassDependency(FRDGBuffer* Buffer, FRDGPass* Pass)
{
	Pass->TraceBuffers.Add(Buffer->Handle);
	Buffer->TracePasses.Add(Pass->Handle);
}

#endif