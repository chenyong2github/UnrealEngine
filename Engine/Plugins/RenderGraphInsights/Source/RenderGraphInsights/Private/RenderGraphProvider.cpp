// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraphProvider.h"

namespace UE
{
namespace RenderGraphInsights
{

INSIGHTS_IMPLEMENT_RTTI(FPacket)
INSIGHTS_IMPLEMENT_RTTI(FScopePacket)
INSIGHTS_IMPLEMENT_RTTI(FResourcePacket)
INSIGHTS_IMPLEMENT_RTTI(FTexturePacket)
INSIGHTS_IMPLEMENT_RTTI(FBufferPacket)
INSIGHTS_IMPLEMENT_RTTI(FPassPacket)
INSIGHTS_IMPLEMENT_RTTI(FGraphPacket)
INSIGHTS_IMPLEMENT_RTTI(FPassIntervalPacket)

FName FRenderGraphProvider::ProviderName("RenderGraphProvider");

void SanitizeName(FString& Name)
{
	if (Name.IsEmpty())
	{
		Name = TEXT("<unnamed>");
	}
}

FString GetSanitizeName(FString Name)
{
	SanitizeName(Name);
	return MoveTemp(Name);
}

FString GetSizeName(uint32 Bytes)
{
	const uint32 KB = 1024;
	const uint32 MB = 1024 * 1024;

	if (Bytes < MB)
	{
		return FString::Printf(TEXT(" (%.3fKB)"), (float)Bytes / (float)KB);
	}
	else
	{
		return FString::Printf(TEXT(" (%.3fMB)"), (float)Bytes / (float)MB);
	}
}

FStringView GetString(const UE::Trace::IAnalyzer::FEventData& EventData, const ANSICHAR* FieldName)
{
	FStringView Value;
	EventData.GetString(FieldName, Value);
	return Value;
}

FPacket::FPacket(const UE::Trace::IAnalyzer::FOnEventContext& Context)
	: Name(GetString(Context.EventData, "Name"))
	, StartTime(Context.EventTime.AsSeconds(Context.EventData.GetValue<uint64>("StartCycles")))
	, EndTime(Context.EventTime.AsSeconds(Context.EventData.GetValue<uint64>("EndCycles")))
{}

FPassIntervalPacket::FPassIntervalPacket(const UE::Trace::IAnalyzer::FOnEventContext& Context)
	: FPacket(Context)
	, FirstPass(Context.EventData.GetValue<FRDGPassHandle>("FirstPass"))
	, LastPass(Context.EventData.GetValue<FRDGPassHandle>("LastPass"))
{}

FScopePacket::FScopePacket(const UE::Trace::IAnalyzer::FOnEventContext& Context)
	: FPassIntervalPacket(Context)
	, Depth(Context.EventData.GetValue<uint16>("Depth"))
{}

FResourcePacket::FResourcePacket(const UE::Trace::IAnalyzer::FOnEventContext& Context)
	: FPassIntervalPacket(Context)
	, Passes(Context.EventData.GetArrayView<FRDGPassHandle>("Passes"))
	, bExternal(Context.EventData.GetValue<bool>("IsExternal"))
	, bExtracted(Context.EventData.GetValue<bool>("IsExtracted"))
	, bCulled(Context.EventData.GetValue<bool>("IsCulled"))
{
	if (Passes.Num())
	{
		FirstPass = Passes[0];
		LastPass  = Passes.Last();
	}
}

FTexturePacket::FTexturePacket(const UE::Trace::IAnalyzer::FOnEventContext& Context)
	: FResourcePacket(Context)
	, Handle(Context.EventData.GetValue<FRDGTextureHandle>("Handle"))
	, NextOwnerHandle(Context.EventData.GetValue<FRDGTextureHandle>("NextOwnerHandle"))
	, SizeInBytes(Context.EventData.GetValue<uint64>("SizeInBytes"))
{
	Desc.Flags = ETextureCreateFlags(Context.EventData.GetValue<uint64>("CreateFlags"));
	Desc.Dimension = ETextureDimension(Context.EventData.GetValue<uint16>("Dimension"));
	Desc.Format = EPixelFormat(Context.EventData.GetValue<uint16>("Format"));
	Desc.Extent.X = Context.EventData.GetValue<uint32>("ExtentX");
	Desc.Extent.Y = Context.EventData.GetValue<uint32>("ExtentY");
	Desc.Depth = Context.EventData.GetValue<uint16>("Depth");
	Desc.ArraySize = Context.EventData.GetValue<uint16>("ArraySize");
	Desc.NumMips = Context.EventData.GetValue<uint8>("NumMips");
	Desc.NumSamples = Context.EventData.GetValue<uint8>("NumSamples");

	Name += GetSizeName(SizeInBytes);
}

FBufferPacket::FBufferPacket(const UE::Trace::IAnalyzer::FOnEventContext& Context)
	: FResourcePacket(Context)
	, Handle(Context.EventData.GetValue<FRDGBufferHandle>("Handle"))
	, NextOwnerHandle(Context.EventData.GetValue<FRDGBufferHandle>("NextOwnerHandle"))
{
	Desc.Usage = EBufferUsageFlags(Context.EventData.GetValue<uint32>("UsageFlags"));
	Desc.BytesPerElement = Context.EventData.GetValue<uint32>("BytesPerElement");
	Desc.NumElements = Context.EventData.GetValue<uint32>("NumElements");

	const uint64 SizeInBytes = Desc.BytesPerElement * Desc.NumElements;
	Name += GetSizeName(SizeInBytes);
}

FPassPacket::FPassPacket(const UE::Trace::IAnalyzer::FOnEventContext& Context)
	: FPacket(Context)
	, Textures(Context.EventData.GetArrayView<FRDGTextureHandle>("Textures"))
	, Buffers(Context.EventData.GetArrayView<FRDGBufferHandle>("Buffers"))
	, Handle(Context.EventData.GetValue<FRDGPassHandle>("Handle"))
	, GraphicsForkPass(Context.EventData.GetValue<FRDGPassHandle>("GraphicsForkPass"))
	, GraphicsJoinPass(Context.EventData.GetValue<FRDGPassHandle>("GraphicsJoinPass"))
	, Flags(ERDGPassFlags(Context.EventData.GetValue<uint16>("Flags")))
	, Pipeline(ERHIPipeline(Context.EventData.GetValue<uint8>("Pipeline")))
	, bCulled(Context.EventData.GetValue<bool>("IsCulled"))
	, bAsyncComputeBegin(Context.EventData.GetValue<bool>("IsAsyncComputeBegin"))
	, bAsyncComputeEnd(Context.EventData.GetValue<bool>("IsAsyncComputeEnd"))
	, bSkipRenderPassBegin(Context.EventData.GetValue<bool>("SkipRenderPassBegin"))
	, bSkipRenderPassEnd(Context.EventData.GetValue<bool>("SkipRenderPassEnd"))
{}

static const uint64 PageSize = 1024;

FGraphPacket::FGraphPacket(TraceServices::ILinearAllocator& Allocator, const UE::Trace::IAnalyzer::FOnEventContext& Context)
	: FPacket(Context)
	, Scopes(Allocator, PageSize)
	, Passes(Allocator, PageSize)
	, Textures(Allocator, PageSize)
	, Buffers(Allocator, PageSize)
	, PassCount(Context.EventData.GetValue<uint16>("PassCount"))
{
	NormalizedPassDuration = (EndTime - StartTime) / double(PassCount);
}

FRenderGraphProvider::FRenderGraphProvider(TraceServices::IAnalysisSession& InSession)
	: Session(InSession)
	, GraphTimeline(Session.GetLinearAllocator())
{}

void FRenderGraphProvider::AddGraph(const UE::Trace::IAnalyzer::FOnEventContext& Context, double& OutEndTime)
{
	CurrentGraph = MakeShared<FGraphPacket>(Session.GetLinearAllocator(), Context);
	SanitizeName(CurrentGraph->Name);

	OutEndTime = CurrentGraph->EndTime;
}

void FRenderGraphProvider::AddGraphEnd()
{
	const double EndTime = CurrentGraph->EndTime;
	const uint64 EventId = GraphTimeline.EmplaceBeginEvent(CurrentGraph->StartTime, MoveTemp(CurrentGraph));
	GraphTimeline.EndEvent(EventId, EndTime);
}

void FRenderGraphProvider::AddScope(FScopePacket InScope)
{
	const FPassPacket& FirstPass = *CurrentGraph->GetPass(InScope.FirstPass);
	const FPassPacket& LastPass  = *CurrentGraph->GetPass(InScope.LastPass);

	InScope.Graph = CurrentGraph.Get();
	InScope.StartTime = FirstPass.StartTime;
	InScope.EndTime = LastPass.EndTime;

	CurrentGraph->ScopeDepth = FMath::Max(CurrentGraph->ScopeDepth, InScope.Depth);
	CurrentGraph->Scopes.EmplaceBack(InScope);
}

void FRenderGraphProvider::AddPass(FPassPacket InPass)
{
	InPass.Graph = CurrentGraph.Get();

	const double NormalizedPassDuration = CurrentGraph->NormalizedPassDuration;
	const uint32 PassIndex = CurrentGraph->Passes.Num();
	InPass.StartTime = CurrentGraph->StartTime + NormalizedPassDuration * double(PassIndex);
	InPass.EndTime = InPass.StartTime + NormalizedPassDuration;

	CurrentGraph->Passes.EmplaceBack(InPass);
}

void FRenderGraphProvider::SetupResource(FResourcePacket& Resource)
{
	Resource.Graph = CurrentGraph.Get();

	if (Resource.bCulled)
	{
		return;
	}

	const FPassPacket* FirstPass = CurrentGraph->GetPass(Resource.FirstPass);
	const FPassPacket* LastPass  = CurrentGraph->GetPass(Resource.LastPass);

	if (Resource.bExternal)
	{
		FirstPass = CurrentGraph->GetProloguePass();
	}

	if (Resource.bExtracted)
	{
		LastPass = CurrentGraph->GetEpiloguePass();
	}

	Resource.StartTime = FirstPass->StartTime;
	Resource.EndTime = LastPass->EndTime;
}

void FRenderGraphProvider::AddTexture(const FTexturePacket InTexture)
{
	FTexturePacket& Texture = CurrentGraph->Textures.EmplaceBack(InTexture);
	Texture.Index = CurrentGraph->Textures.Num() - 1;
	SetupResource(Texture);

	if (const FTexturePacket* const* PreviousOwnerPtr = CurrentGraph->TextureHandleToPreviousOwner.Find(Texture.Handle))
	{
		const FTexturePacket* PreviousOwner = *PreviousOwnerPtr;
		Texture.PreviousOwner = PreviousOwner;
		Texture.Depth = PreviousOwner->Depth;
	}
	else if (!Texture.bCulled)
	{
		Texture.Depth = CurrentGraph->TextureDepthOffset++;
	}

	if (Texture.NextOwnerHandle.IsValid())
	{
		CurrentGraph->TextureHandleToPreviousOwner.Emplace(Texture.NextOwnerHandle, &Texture);
	}
}

void FRenderGraphProvider::AddBuffer(const FBufferPacket InBuffer)
{
	FBufferPacket& Buffer = CurrentGraph->Buffers.EmplaceBack(InBuffer);
	Buffer.Index = CurrentGraph->Buffers.Num() - 1;
	SetupResource(Buffer);

	if (const FBufferPacket* const* PreviousOwnerPtr = CurrentGraph->BufferHandleToPreviousOwner.Find(Buffer.Handle))
	{
		const FBufferPacket* PreviousOwner = *PreviousOwnerPtr;
		Buffer.PreviousOwner = PreviousOwner;
		Buffer.Depth = PreviousOwner->Depth;
	}
	else if (!Buffer.bCulled)
	{
		Buffer.Depth = CurrentGraph->BufferDepthOffset++;
	}

	if (Buffer.NextOwnerHandle.IsValid())
	{
		CurrentGraph->BufferHandleToPreviousOwner.Emplace(Buffer.NextOwnerHandle, &Buffer);
	}
}

} //namespace RenderGraphInsights
} //namespace UE