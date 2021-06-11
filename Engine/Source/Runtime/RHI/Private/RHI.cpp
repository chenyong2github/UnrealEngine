// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RHI.cpp: Render Hardware Interface implementation.
=============================================================================*/

#include "RHI.h"
#include "Modules/ModuleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/MessageDialog.h"
#include "RHIShaderFormatDefinitions.inl"
#include "ProfilingDebugging/CsvProfiler.h"
#include "String/LexFromString.h"
#include "String/ParseTokens.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, RHI);

/** RHI Logging. */
DEFINE_LOG_CATEGORY(LogRHI);
CSV_DEFINE_CATEGORY(RHI, true);

#if UE_BUILD_SHIPPING
CSV_DEFINE_CATEGORY(DrawCall, false);
#else
CSV_DEFINE_CATEGORY(DrawCall, true);
#endif

// Define counter stats.
DEFINE_STAT(STAT_RHIDrawPrimitiveCalls);
DEFINE_STAT(STAT_RHITriangles);
DEFINE_STAT(STAT_RHILines);

// Define memory stats.
DEFINE_STAT(STAT_RenderTargetMemory2D);
DEFINE_STAT(STAT_RenderTargetMemory3D);
DEFINE_STAT(STAT_RenderTargetMemoryCube);
DEFINE_STAT(STAT_TextureMemory2D);
DEFINE_STAT(STAT_TextureMemory3D);
DEFINE_STAT(STAT_TextureMemoryCube);
DEFINE_STAT(STAT_UniformBufferMemory);
DEFINE_STAT(STAT_IndexBufferMemory);
DEFINE_STAT(STAT_VertexBufferMemory);
DEFINE_STAT(STAT_StructuredBufferMemory);
DEFINE_STAT(STAT_PixelBufferMemory);

IMPLEMENT_TYPE_LAYOUT(FRHIUniformBufferLayout);
IMPLEMENT_TYPE_LAYOUT(FRHIUniformBufferLayout::FResourceParameter);

#if !defined(RHIRESOURCE_NUM_FRAMES_TO_EXPIRE)
	#define RHIRESOURCE_NUM_FRAMES_TO_EXPIRE 3
#endif

static FAutoConsoleVariable CVarUseVulkanRealUBs(
	TEXT("r.Vulkan.UseRealUBs"),
	1,
	TEXT("0: Emulate uniform buffers on Vulkan SM4/SM5 (debugging ONLY)\n")
	TEXT("1: Use real uniform buffers [default]"),
	ECVF_ReadOnly
	);

static TAutoConsoleVariable<int32> CVarDisableEngineAndAppRegistration(
	TEXT("r.DisableEngineAndAppRegistration"),
	0,
	TEXT("If true, disables engine and app registration, to disable GPU driver optimizations during debugging and development\n")
	TEXT("Changes will only take effect in new game/editor instances - can't be changed at runtime.\n"),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarGraphicsAdapter(
	TEXT("r.GraphicsAdapter"),
	-1,
	TEXT("User request to pick a specific graphics adapter (e.g. when using a integrated graphics card with a discrete one)\n")
	TEXT("For Windows D3D, unless a specific adapter is chosen we reject Microsoft adapters because we don't want the software emulation.\n")
	TEXT("This takes precedence over -prefer{AMD|NVidia|Intel} when the value is >= 0.\n")
	TEXT(" -2: Take the first one that fulfills the criteria\n")
	TEXT(" -1: Favour non integrated because there are usually faster (default)\n")
	TEXT("  0: Adapter #0\n")
	TEXT("  1: Adapter #1, ..."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

template<typename EnumType>
inline FString BuildEnumNameBitList(EnumType Value, const TCHAR*(*GetEnumName)(EnumType))
{
	if (Value == EnumType(0))
	{
		return GetEnumName(Value);
	}

	using T = __underlying_type(EnumType);
	T StateValue = (T)Value;

	FString Name;

	int32 BitIndex = 0;
	while (StateValue)
	{
		if (StateValue & 1)
		{
			if (Name.Len() > 0 && StateValue > 0)
			{
				Name += TEXT("|");
			}

			Name += GetEnumName(EnumType(T(1) << BitIndex));
		}

		BitIndex++;
		StateValue >>= 1;
	}

	return MoveTemp(Name);
}

FString GetRHIAccessName(ERHIAccess Access)
{
	switch (Access)
	{
		// Cases for legacy resource state, to make the huge bit combinations easier to read...
	case ERHIAccess::EReadable:  return TEXT("EReadable");
	case ERHIAccess::EWritable:  return TEXT("EWritable");
	case ERHIAccess::ERWBarrier: return TEXT("ERWBarrier");

		// All other states are built as a logic OR of state bits.
	default:
		return BuildEnumNameBitList<ERHIAccess>(Access, [](ERHIAccess AccessBit)
		{
			switch (AccessBit)
			{
			default: checkNoEntry(); // fall through
			case ERHIAccess::Unknown:             return TEXT("Unknown");
			case ERHIAccess::CPURead:             return TEXT("CPURead");
			case ERHIAccess::Present:             return TEXT("Present");
			case ERHIAccess::IndirectArgs:        return TEXT("IndirectArgs");
			case ERHIAccess::VertexOrIndexBuffer: return TEXT("VertexOrIndexBuffer");
			case ERHIAccess::SRVCompute:          return TEXT("SRVCompute");
			case ERHIAccess::SRVGraphics:         return TEXT("SRVGraphics");
			case ERHIAccess::CopySrc:             return TEXT("CopySrc");
			case ERHIAccess::ResolveSrc:          return TEXT("ResolveSrc");
			case ERHIAccess::DSVRead:             return TEXT("DSVRead");
			case ERHIAccess::UAVCompute:          return TEXT("UAVCompute");
			case ERHIAccess::UAVGraphics:         return TEXT("UAVGraphics");
			case ERHIAccess::RTV:                 return TEXT("RTV");
			case ERHIAccess::CopyDest:            return TEXT("CopyDest");
			case ERHIAccess::ResolveDst:          return TEXT("ResolveDst");
			case ERHIAccess::DSVWrite:            return TEXT("DSVWrite");
			case ERHIAccess::ShadingRateSource:	  return TEXT("ShadingRateSource");
			}
		});
	}
}

FString GetResourceTransitionFlagsName(EResourceTransitionFlags Flags)
{
	return BuildEnumNameBitList<EResourceTransitionFlags>(Flags, [](EResourceTransitionFlags Value)
	{
		switch (Value)
		{
		default: checkNoEntry(); // fall through
		case EResourceTransitionFlags::None:                return TEXT("None");
		case EResourceTransitionFlags::MaintainCompression: return TEXT("MaintainCompression");
		}
	});
}

FString GetRHIPipelineName(ERHIPipeline Pipeline)
{
	return BuildEnumNameBitList<ERHIPipeline>(Pipeline, [](ERHIPipeline Value)
	{
		if (Value == ERHIPipeline(0)) { return TEXT("None"); }

		switch (Value)
		{
		default: checkNoEntry(); // fall through
		case ERHIPipeline::Graphics:     return TEXT("Graphics");
		case ERHIPipeline::AsyncCompute: return TEXT("AsyncCompute");
		}
	});
}


#if STATS
#include "Stats/StatsData.h"
static void DumpRHIMemory(FOutputDevice& OutputDevice)
{
	TArray<FStatMessage> Stats;
	GetPermanentStats(Stats);

	FName NAME_STATGROUP_RHI(FStatGroup_STATGROUP_RHI::GetGroupName());
	OutputDevice.Logf(TEXT("RHI resource memory (not tracked by our allocator)"));
	int64 TotalMemory = 0;
	for (int32 Index = 0; Index < Stats.Num(); Index++)
	{
		FStatMessage const& Meta = Stats[Index];
		FName LastGroup = Meta.NameAndInfo.GetGroupName();
		if (LastGroup == NAME_STATGROUP_RHI && Meta.NameAndInfo.GetFlag(EStatMetaFlags::IsMemory))
		{
			OutputDevice.Logf(TEXT("%s"), *FStatsUtils::DebugPrint(Meta));
			TotalMemory += Meta.GetValue_int64();
		}
	}
	OutputDevice.Logf(TEXT("%.3fMB total"), TotalMemory / 1024.f / 1024.f);
}

static FAutoConsoleCommandWithOutputDevice GDumpRHIMemoryCmd(
	TEXT("rhi.DumpMemory"),
	TEXT("Dumps RHI memory stats to the log"),
	FConsoleCommandWithOutputDeviceDelegate::CreateStatic(DumpRHIMemory)
	);
#endif

//DO NOT USE THE STATIC FLINEARCOLORS TO INITIALIZE THIS STUFF.  
//Static init order is undefined and you will likely end up with bad values on some platforms.
const FClearValueBinding FClearValueBinding::None(EClearBinding::ENoneBound);
const FClearValueBinding FClearValueBinding::Black(FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
const FClearValueBinding FClearValueBinding::BlackMaxAlpha(FLinearColor(0.0f, 0.0f, 0.0f, FLT_MAX));
const FClearValueBinding FClearValueBinding::White(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
const FClearValueBinding FClearValueBinding::Transparent(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
const FClearValueBinding FClearValueBinding::DepthOne(1.0f, 0);
const FClearValueBinding FClearValueBinding::DepthZero(0.0f, 0);
const FClearValueBinding FClearValueBinding::DepthNear((float)ERHIZBuffer::NearPlane, 0);
const FClearValueBinding FClearValueBinding::DepthFar((float)ERHIZBuffer::FarPlane, 0);
const FClearValueBinding FClearValueBinding::Green(FLinearColor(0.0f, 1.0f, 0.0f, 1.0f));
// Note: this is used as the default normal for DBuffer decals.  It must decode to a value of 0 in DecodeDBufferData.
const FClearValueBinding FClearValueBinding::DefaultNormal8Bit(FLinearColor(128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f, 1.0f));

TLockFreePointerListUnordered<FRHIResource, PLATFORM_CACHE_LINE_SIZE> FRHIResource::PendingDeletes;
FRHIResource* FRHIResource::CurrentlyDeleting = nullptr;
TArray<FRHIResource::ResourcesToDelete> FRHIResource::DeferredDeletionQueue;
uint32 FRHIResource::CurrentFrame = 0;
RHI_API FDrawCallCategoryName* FDrawCallCategoryName::Array[FDrawCallCategoryName::MAX_DRAWCALL_CATEGORY];
RHI_API int32 FDrawCallCategoryName::DisplayCounts[FDrawCallCategoryName::MAX_DRAWCALL_CATEGORY][MAX_NUM_GPUS];
RHI_API int32 FDrawCallCategoryName::NumCategory = 0;

FString FVertexElement::ToString() const
{
	return FString::Printf(TEXT("<%u %u %u %u %u %u>")
		, uint32(StreamIndex)
		, uint32(Offset)
		, uint32(Type)
		, uint32(AttributeIndex)
		, uint32(Stride)
		, uint32(bUseInstanceIndex)
	);
}

void FVertexElement::FromString(const FString& InSrc)
{
	FromString(FStringView(InSrc));
}

void FVertexElement::FromString(const FStringView& InSrc)
{
	constexpr int32 PartCount = 6;

	TArray<FStringView, TInlineAllocator<PartCount>> Parts;
	UE::String::ParseTokensMultiple(InSrc.TrimStartAndEnd(), {TEXT('\r'), TEXT('\n'), TEXT('\t'), TEXT('<'), TEXT('>'), TEXT(' ')},
		[&Parts](FStringView Part) { if (!Part.IsEmpty()) { Parts.Add(Part); } });

	check(Parts.Num() == PartCount && sizeof(Type) == 1); //not a very robust parser
	const FStringView* PartIt = Parts.GetData();
	LexFromString(StreamIndex, *PartIt++);
	LexFromString(Offset, *PartIt++);
	LexFromString((uint8&)Type, *PartIt++);
	LexFromString(AttributeIndex, *PartIt++);
	LexFromString(Stride, *PartIt++);
	LexFromString(bUseInstanceIndex, *PartIt++);
	check(Parts.GetData() + PartCount == PartIt);
}

uint32 GetTypeHash(const FSamplerStateInitializerRHI& Initializer)
{
	uint32 Hash = GetTypeHash(Initializer.Filter);
	Hash = HashCombine(Hash, GetTypeHash(Initializer.AddressU));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.AddressV));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.AddressW));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.MipBias));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.MinMipLevel));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.MaxMipLevel));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.MaxAnisotropy));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.BorderColor));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.SamplerComparisonFunction));
	return Hash;
}

bool operator== (const FSamplerStateInitializerRHI& A, const FSamplerStateInitializerRHI& B)
{
	bool bSame = 
		A.Filter == B.Filter &&
		A.AddressU == B.AddressU &&
		A.AddressV == B.AddressV &&
		A.AddressW == B.AddressW &&
		A.MipBias == B.MipBias &&
		A.MinMipLevel == B.MinMipLevel &&
		A.MaxMipLevel == B.MaxMipLevel &&
		A.MaxAnisotropy == B.MaxAnisotropy &&
		A.BorderColor == B.BorderColor &&
		A.SamplerComparisonFunction == B.SamplerComparisonFunction;
	return bSame;
}

uint32 GetTypeHash(const FRasterizerStateInitializerRHI& Initializer)
{
	uint32 Hash = GetTypeHash(Initializer.FillMode);
	Hash = HashCombine(Hash, GetTypeHash(Initializer.CullMode));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.DepthBias));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.SlopeScaleDepthBias));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.bAllowMSAA));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.bEnableLineAA));
	return Hash;
}
	
bool operator== (const FRasterizerStateInitializerRHI& A, const FRasterizerStateInitializerRHI& B)
{
	bool bSame = 
		A.FillMode == B.FillMode && 
		A.CullMode == B.CullMode && 
		A.DepthBias == B.DepthBias && 
		A.SlopeScaleDepthBias == B.SlopeScaleDepthBias && 
		A.bAllowMSAA == B.bAllowMSAA && 
		A.bEnableLineAA == B.bEnableLineAA;
	return bSame;
}

uint32 GetTypeHash(const FDepthStencilStateInitializerRHI& Initializer)
{
	uint32 Hash = GetTypeHash(Initializer.bEnableDepthWrite);
	Hash = HashCombine(Hash, GetTypeHash(Initializer.DepthTest));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.bEnableFrontFaceStencil));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.FrontFaceStencilTest));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.FrontFaceStencilFailStencilOp));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.FrontFaceDepthFailStencilOp));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.FrontFacePassStencilOp));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.bEnableBackFaceStencil));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.BackFaceStencilTest));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.BackFaceStencilFailStencilOp));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.BackFaceDepthFailStencilOp));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.BackFacePassStencilOp));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.StencilReadMask));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.StencilWriteMask));
	return Hash;
}

bool operator== (const FDepthStencilStateInitializerRHI& A, const FDepthStencilStateInitializerRHI& B)
{
	bool bSame = 
		A.bEnableDepthWrite == B.bEnableDepthWrite && 
		A.DepthTest == B.DepthTest && 
		A.bEnableFrontFaceStencil == B.bEnableFrontFaceStencil && 
		A.FrontFaceStencilTest == B.FrontFaceStencilTest && 
		A.FrontFaceStencilFailStencilOp == B.FrontFaceStencilFailStencilOp && 
		A.FrontFaceDepthFailStencilOp == B.FrontFaceDepthFailStencilOp && 
		A.FrontFacePassStencilOp == B.FrontFacePassStencilOp && 
		A.bEnableBackFaceStencil == B.bEnableBackFaceStencil && 
		A.BackFaceStencilTest == B.BackFaceStencilTest && 
		A.BackFaceStencilFailStencilOp == B.BackFaceStencilFailStencilOp && 
		A.BackFaceDepthFailStencilOp == B.BackFaceDepthFailStencilOp && 
		A.BackFacePassStencilOp == B.BackFacePassStencilOp && 
		A.StencilReadMask == B.StencilReadMask && 
		A.StencilWriteMask == B.StencilWriteMask;
	return bSame;
}

FString FDepthStencilStateInitializerRHI::ToString() const
{
	return
		FString::Printf(TEXT("<%u %u ")
			, uint32(!!bEnableDepthWrite)
			, uint32(DepthTest)
		)
		+ FString::Printf(TEXT("%u %u %u %u %u ")
			, uint32(!!bEnableFrontFaceStencil)
			, uint32(FrontFaceStencilTest)
			, uint32(FrontFaceStencilFailStencilOp)
			, uint32(FrontFaceDepthFailStencilOp)
			, uint32(FrontFacePassStencilOp)
		)
		+ FString::Printf(TEXT("%u %u %u %u %u ")
			, uint32(!!bEnableBackFaceStencil)
			, uint32(BackFaceStencilTest)
			, uint32(BackFaceStencilFailStencilOp)
			, uint32(BackFaceDepthFailStencilOp)
			, uint32(BackFacePassStencilOp)
		)
		+ FString::Printf(TEXT("%u %u>")
			, uint32(StencilReadMask)
			, uint32(StencilWriteMask)
		);
}

void FDepthStencilStateInitializerRHI::FromString(const FString& InSrc)
{
	FromString(FStringView(InSrc));
}

void FDepthStencilStateInitializerRHI::FromString(const FStringView& InSrc)
{
	constexpr int32 PartCount = 14;

	TArray<FStringView, TInlineAllocator<PartCount>> Parts;
	UE::String::ParseTokensMultiple(InSrc.TrimStartAndEnd(), {TEXT('\r'), TEXT('\n'), TEXT('\t'), TEXT('<'), TEXT('>'), TEXT(' ')},
		[&Parts](FStringView Part) { if (!Part.IsEmpty()) { Parts.Add(Part); } });

	check(Parts.Num() == PartCount && sizeof(bool) == 1 && sizeof(FrontFaceStencilFailStencilOp) == 1 && sizeof(BackFaceStencilTest) == 1 && sizeof(BackFaceDepthFailStencilOp) == 1); //not a very robust parser

	const FStringView* PartIt = Parts.GetData();

	LexFromString((uint8&)bEnableDepthWrite, *PartIt++);
	LexFromString((uint8&)DepthTest, *PartIt++);

	LexFromString((uint8&)bEnableFrontFaceStencil, *PartIt++);
	LexFromString((uint8&)FrontFaceStencilTest, *PartIt++);
	LexFromString((uint8&)FrontFaceStencilFailStencilOp, *PartIt++);
	LexFromString((uint8&)FrontFaceDepthFailStencilOp, *PartIt++);
	LexFromString((uint8&)FrontFacePassStencilOp, *PartIt++);

	LexFromString((uint8&)bEnableBackFaceStencil, *PartIt++);
	LexFromString((uint8&)BackFaceStencilTest, *PartIt++);
	LexFromString((uint8&)BackFaceStencilFailStencilOp, *PartIt++);
	LexFromString((uint8&)BackFaceDepthFailStencilOp, *PartIt++);
	LexFromString((uint8&)BackFacePassStencilOp, *PartIt++);

	LexFromString(StencilReadMask, *PartIt++);
	LexFromString(StencilWriteMask, *PartIt++);

	check(Parts.GetData() + PartCount == PartIt);
}

FString FBlendStateInitializerRHI::ToString() const
{
	FString Result = TEXT("<");
	for (int32 Index = 0; Index < MaxSimultaneousRenderTargets; Index++)
	{
		Result += RenderTargets[Index].ToString();
	}
	Result += FString::Printf(TEXT("%d %d>"), uint32(!!bUseIndependentRenderTargetBlendStates), uint32(!!bUseAlphaToCoverage));
	return Result;
}

void FBlendStateInitializerRHI::FromString(const FString& InSrc)
{
	FromString(FStringView(InSrc));
}

void FBlendStateInitializerRHI::FromString(const FStringView& InSrc)
{
	// files written before bUseAlphaToCoverage change (added in CL 13846572) have one less part
	constexpr int32 BackwardCompatiblePartCount = MaxSimultaneousRenderTargets * FRenderTarget::NUM_STRING_FIELDS + 1;
	constexpr int32 PartCount = BackwardCompatiblePartCount + 1;

	TArray<FStringView, TInlineAllocator<PartCount>> Parts;
	UE::String::ParseTokensMultiple(InSrc.TrimStartAndEnd(), {TEXT('\r'), TEXT('\n'), TEXT('\t'), TEXT('<'), TEXT('>'), TEXT(' ')},
		[&Parts](FStringView Part) { if (!Part.IsEmpty()) { Parts.Add(Part); } });

	checkf((Parts.Num() == PartCount || Parts.Num() == BackwardCompatiblePartCount) && sizeof(bool) == 1, 
		TEXT("Expecting %d (or %d, for an older format) parts in the blendstate string, got %d"), PartCount, BackwardCompatiblePartCount, Parts.Num()); //not a very robust parser
	bool bHasAlphaToCoverageField = Parts.Num() == PartCount;

	const FStringView* PartIt = Parts.GetData();
	for (int32 Index = 0; Index < MaxSimultaneousRenderTargets; Index++)
	{
		RenderTargets[Index].FromString(MakeArrayView(PartIt, FRenderTarget::NUM_STRING_FIELDS));
		PartIt += FRenderTarget::NUM_STRING_FIELDS;
	}
	LexFromString((int8&)bUseIndependentRenderTargetBlendStates, *PartIt++);
	if (bHasAlphaToCoverageField)
	{
		LexFromString((int8&)bUseAlphaToCoverage, *PartIt++);
		check(Parts.GetData() + PartCount == PartIt);
	}
	else
	{
		bUseAlphaToCoverage = false;
		check(Parts.GetData() + BackwardCompatiblePartCount == PartIt);
	}
}

uint32 GetTypeHash(const FBlendStateInitializerRHI& Initializer)
{
	uint32 Hash = GetTypeHash(Initializer.bUseIndependentRenderTargetBlendStates);
	for (int32 i = 0; i < MaxSimultaneousRenderTargets; ++i)
	{
		Hash = HashCombine(Hash, GetTypeHash(Initializer.RenderTargets[i]));
	}
	
	return Hash;
}

bool operator== (const FBlendStateInitializerRHI& A, const FBlendStateInitializerRHI& B)
{
	bool bSame = A.bUseIndependentRenderTargetBlendStates == B.bUseIndependentRenderTargetBlendStates;
	for (int32 i = 0; i < MaxSimultaneousRenderTargets && bSame; ++i)
	{
		bSame = bSame && A.RenderTargets[i] == B.RenderTargets[i];
	}
	return bSame;
}


FString FBlendStateInitializerRHI::FRenderTarget::ToString() const
{
	return FString::Printf(TEXT("%u %u %u %u %u %u %u ")
		, uint32(ColorBlendOp)
		, uint32(ColorSrcBlend)
		, uint32(ColorDestBlend)
		, uint32(AlphaBlendOp)
		, uint32(AlphaSrcBlend)
		, uint32(AlphaDestBlend)
		, uint32(ColorWriteMask)
	);
}

void FBlendStateInitializerRHI::FRenderTarget::FromString(const TArray<FString>& Parts, int32 Index)
{
	check(Index + NUM_STRING_FIELDS <= Parts.Num());
	LexFromString((uint8&)ColorBlendOp, *Parts[Index++]);
	LexFromString((uint8&)ColorSrcBlend, *Parts[Index++]);
	LexFromString((uint8&)ColorDestBlend, *Parts[Index++]);
	LexFromString((uint8&)AlphaBlendOp, *Parts[Index++]);
	LexFromString((uint8&)AlphaSrcBlend, *Parts[Index++]);
	LexFromString((uint8&)AlphaDestBlend, *Parts[Index++]);
	LexFromString((uint8&)ColorWriteMask, *Parts[Index++]);
}

void FBlendStateInitializerRHI::FRenderTarget::FromString(TArrayView<const FStringView> Parts)
{
	check(Parts.Num() == NUM_STRING_FIELDS);
	const FStringView* PartIt = Parts.GetData();
	LexFromString((uint8&)ColorBlendOp, *PartIt++);
	LexFromString((uint8&)ColorSrcBlend, *PartIt++);
	LexFromString((uint8&)ColorDestBlend, *PartIt++);
	LexFromString((uint8&)AlphaBlendOp, *PartIt++);
	LexFromString((uint8&)AlphaSrcBlend, *PartIt++);
	LexFromString((uint8&)AlphaDestBlend, *PartIt++);
	LexFromString((uint8&)ColorWriteMask, *PartIt++);
}

uint32 GetTypeHash(const FBlendStateInitializerRHI::FRenderTarget& Initializer)
{
	uint32 Hash = GetTypeHash(Initializer.ColorBlendOp);
	Hash = HashCombine(Hash, GetTypeHash(Initializer.ColorDestBlend));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.ColorSrcBlend));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.AlphaBlendOp));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.AlphaDestBlend));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.AlphaSrcBlend));
	Hash = HashCombine(Hash, GetTypeHash(Initializer.ColorWriteMask));
	return Hash;
}

bool operator==(const FBlendStateInitializerRHI::FRenderTarget& A, const FBlendStateInitializerRHI::FRenderTarget& B)
{
	bool bSame = 
		A.ColorBlendOp == B.ColorBlendOp && 
		A.ColorDestBlend == B.ColorDestBlend && 
		A.ColorSrcBlend == B.ColorSrcBlend && 
		A.AlphaBlendOp == B.AlphaBlendOp && 
		A.AlphaDestBlend == B.AlphaDestBlend && 
		A.AlphaSrcBlend == B.AlphaSrcBlend && 
		A.ColorWriteMask == B.ColorWriteMask;
	return bSame;
}

bool FRHIResource::Bypass()
{
	return GRHICommandList.Bypass();
}

DECLARE_CYCLE_STAT(TEXT("Delete Resources"), STAT_DeleteResources, STATGROUP_RHICMDLIST);

void FRHIResource::FlushPendingDeletes(bool bFlushDeferredDeletes)
{
	SCOPE_CYCLE_COUNTER(STAT_DeleteResources);

	check(IsInRenderingThread());

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
#if ENABLE_RHI_VALIDATION
	if (GDynamicRHI)
	{
		// Submit all remaining work to the GPU. This also ensures that validation RHI barrier tracking
		// operations have been flushed before we delete any resources they could be referring to.
		RHICmdList.SubmitCommandsHint();
	}
#endif
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	FRHICommandListExecutor::CheckNoOutstandingCmdLists();
	if (GDynamicRHI)
	{
		GDynamicRHI->RHIPerFrameRHIFlushComplete();
	}

	auto Delete = [](TArray<FRHIResource*>& ToDelete)
	{
		for (int32 Index = 0; Index < ToDelete.Num(); Index++)
		{
			FRHIResource* Ref = ToDelete[Index];
			check(Ref->MarkedForDelete == 1);
			if (Ref->GetRefCount() == 0) // caches can bring dead objects back to life
			{
				CurrentlyDeleting = Ref;
				delete Ref;
				CurrentlyDeleting = nullptr;
			}
			else
			{
				Ref->MarkedForDelete = 0;
				FPlatformMisc::MemoryBarrier();
			}
		}
	};

	while (1)
	{
		if (PendingDeletes.IsEmpty())
		{
			break;
		}
		if (PlatformNeedsExtraDeletionLatency())
		{
			const int32 Index = DeferredDeletionQueue.AddDefaulted();
			ResourcesToDelete& ResourceBatch = DeferredDeletionQueue[Index];
			ResourceBatch.FrameDeleted = CurrentFrame;
			PendingDeletes.PopAll(ResourceBatch.Resources);
			check(ResourceBatch.Resources.Num());
		}
		else
		{
			TArray<FRHIResource*> ToDelete;
			PendingDeletes.PopAll(ToDelete);
			check(ToDelete.Num());
			Delete(ToDelete);
		}
	}

	const uint32 NumFramesToExpire = RHIRESOURCE_NUM_FRAMES_TO_EXPIRE;

	if (DeferredDeletionQueue.Num())
	{
		if (bFlushDeferredDeletes)
		{
			FRHICommandListExecutor::GetImmediateCommandList().BlockUntilGPUIdle();

			for (int32 Idx = 0; Idx < DeferredDeletionQueue.Num(); ++Idx)
			{
				ResourcesToDelete& ResourceBatch = DeferredDeletionQueue[Idx];
				Delete(ResourceBatch.Resources);
			}

			DeferredDeletionQueue.Empty();
		}
		else
		{
			int32 DeletedBatchCount = 0;
			while (DeletedBatchCount < DeferredDeletionQueue.Num())
			{
				ResourcesToDelete& ResourceBatch = DeferredDeletionQueue[DeletedBatchCount];
				if (((ResourceBatch.FrameDeleted + NumFramesToExpire) < CurrentFrame) || !GIsRHIInitialized)
				{
					Delete(ResourceBatch.Resources);
					++DeletedBatchCount;
				}
				else
				{
					break;
				}
			}

			if (DeletedBatchCount)
			{
				DeferredDeletionQueue.RemoveAt(0, DeletedBatchCount);
			}
		}

		++CurrentFrame;
	}
}

static_assert(ERHIZBuffer::FarPlane != ERHIZBuffer::NearPlane, "Near and Far planes must be different!");
static_assert((int32)ERHIZBuffer::NearPlane == 0 || (int32)ERHIZBuffer::NearPlane == 1, "Invalid Values for Near Plane, can only be 0 or 1!");
static_assert((int32)ERHIZBuffer::FarPlane == 0 || (int32)ERHIZBuffer::FarPlane == 1, "Invalid Values for Far Plane, can only be 0 or 1");


/**
 * RHI configuration settings.
 */

static TAutoConsoleVariable<int32> ResourceTableCachingCvar(
	TEXT("rhi.ResourceTableCaching"),
	1,
	TEXT("If 1, the RHI will cache resource table contents within a frame. Otherwise resource tables are rebuilt for every draw call.")
	);
static TAutoConsoleVariable<int32> GSaveScreenshotAfterProfilingGPUCVar(
	TEXT("r.ProfileGPU.Screenshot"),
	1,
	TEXT("Whether a screenshot should be taken when profiling the GPU. 0:off, 1:on (default)"),
	ECVF_RenderThreadSafe);
static TAutoConsoleVariable<int32> GShowProfilerAfterProfilingGPUCVar(
	TEXT("r.ProfileGPU.ShowUI"),
	1,
	TEXT("Whether the user interface profiler should be displayed after profiling the GPU.\n")
	TEXT("The results will always go to the log/console\n")
	TEXT("0:off, 1:on (default)"),
	ECVF_RenderThreadSafe);
static TAutoConsoleVariable<float> GGPUHitchThresholdCVar(
	TEXT("RHI.GPUHitchThreshold"),
	100.0f,
	TEXT("Threshold for detecting hitches on the GPU (in milliseconds).")
	);
static TAutoConsoleVariable<int32> GCVarRHIRenderPass(
	TEXT("r.RHIRenderPasses"),
	0,
	TEXT(""),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarGPUCrashDebugging(
	TEXT("r.GPUCrashDebugging"),
	0,
	TEXT("Enable vendor specific GPU crash analysis tools"),
	ECVF_ReadOnly
	);

static TAutoConsoleVariable<int32> CVarGPUCrashDump(
	TEXT("r.GPUCrashDump"),
	0,
	TEXT("Enable vendor specific GPU crash dumps"),
	ECVF_ReadOnly
);

static TAutoConsoleVariable<int32> CVarGPUCrashDebuggingAftermathMarkers(
	TEXT("r.GPUCrashDebugging.Aftermath.Markers"),
	0,
	TEXT("Enable draw event markers in Aftermath dumps"),
	ECVF_ReadOnly
);

static TAutoConsoleVariable<int32> CVarGPUCrashDebuggingAftermathCallstack(
	TEXT("r.GPUCrashDebugging.Aftermath.Callstack"),
	0,
	TEXT("Enable callstack capture in Aftermath dumps"),
	ECVF_ReadOnly
);

static TAutoConsoleVariable<int32> CVarGPUCrashDebuggingAftermathResourceTracking(
	TEXT("r.GPUCrashDebugging.Aftermath.ResourceTracking"),
	0,
	TEXT("Enable resource tracking for Aftermath dumps"),
	ECVF_ReadOnly
);

static TAutoConsoleVariable<int32> CVarGPUCrashDebuggingAftermathTrackAll(
	TEXT("r.GPUCrashDebugging.Aftermath.TrackAll"),
	1,
	TEXT("Enable maximum tracking for Aftermath dumps"),
	ECVF_ReadOnly
);

static FAutoConsoleVariableRef CVarEnableVariableRateShading(
	TEXT("r.VRS.Enable"),
	GRHIVariableRateShadingEnabled,
	TEXT("Toggle to enable Variable Rate Shading."),
	ECVF_RenderThreadSafe);

static FAutoConsoleVariableRef CVarEnableAttachmentVariableRateShading(
	TEXT("r.VRS.EnableImage"),
	GRHIAttachmentVariableRateShadingEnabled,
	TEXT("Toggle to enable image-based Variable Rate Shading."),
	ECVF_RenderThreadSafe);

namespace RHIConfig
{
	bool ShouldSaveScreenshotAfterProfilingGPU()
	{
		return GSaveScreenshotAfterProfilingGPUCVar.GetValueOnAnyThread() != 0;
	}

	bool ShouldShowProfilerAfterProfilingGPU()
	{
		return GShowProfilerAfterProfilingGPUCVar.GetValueOnAnyThread() != 0;
	}

	float GetGPUHitchThreshold()
	{
		return GGPUHitchThresholdCVar.GetValueOnAnyThread() * 0.001f;
	}
}

/**
 * RHI globals.
 */

bool GIsRHIInitialized = false;
int32 GMaxTextureMipCount = MAX_TEXTURE_MIP_COUNT;
bool GRHISupportsCopyToTextureMultipleMips = false;
bool GSupportsQuadBufferStereo = false;
FString GRHIAdapterName;
FString GRHIAdapterInternalDriverVersion;
FString GRHIAdapterUserDriverVersion;
FString GRHIAdapterDriverDate;
uint32 GRHIVendorId = 0;
uint32 GRHIDeviceId = 0;
uint32 GRHIDeviceRevision = 0;
bool GRHIDeviceIsAMDPreGCNArchitecture = false;
bool GSupportsRenderDepthTargetableShaderResources = true;
TRHIGlobal<bool> GSupportsRenderTargetFormat_PF_G8(true);
TRHIGlobal<bool> GSupportsRenderTargetFormat_PF_FloatRGBA(true);
bool GSupportsShaderFramebufferFetch = false;
bool GSupportsShaderDepthStencilFetch = false;
bool GSupportsTimestampRenderQueries = false;
bool GRHISupportsGPUTimestampBubblesRemoval = false;
bool GRHISupportsFrameCyclesBubblesRemoval = false;
bool GHardwareHiddenSurfaceRemoval = false;
bool GRHISupportsAsyncTextureCreation = false;
bool GRHISupportsQuadTopology = false;
bool GRHISupportsRectTopology = false;
bool GRHISupportsPrimitiveShaders = false;
bool GRHISupportsAtomicUInt64 = false;
bool GRHISupportsResummarizeHTile = false;
bool GRHISupportsExplicitHTile = false;
bool GRHISupportsExplicitFMask = false;
bool GRHISupportsDepthUAV = false;
bool GSupportsParallelRenderingTasksWithSeparateRHIThread = true;
bool GRHIThreadNeedsKicking = false;
int32 GRHIMaximumReccommendedOustandingOcclusionQueries = MAX_int32;
bool GRHISupportsExactOcclusionQueries = true;
bool GSupportsVolumeTextureRendering = true;
bool GSupportsSeparateRenderTargetBlendState = false;
bool GRHINeedsUnatlasedCSMDepthsWorkaround = false;
bool GSupportsTexture3D = true;
bool GSupportsMobileMultiView = false;
bool GSupportsImageExternal = false;
bool GSupportsResourceView = true;
bool GRHISupportsDrawIndirect = true;
bool GRHISupportsMultithreading = false;
bool GSupportsWideMRT = true;
float GMinClipZ = 0.0f;
float GProjectionSignY = 1.0f;
bool GRHINeedsExtraDeletionLatency = false;
bool GRHIForceNoDeletionLatencyForStreamingTextures = false;
TRHIGlobal<int32> GMaxComputeDispatchDimension((1 << 16) - 1);
bool GRHILazyShaderCodeLoading = false;
bool GRHISupportsLazyShaderCodeLoading = false;
TRHIGlobal<int32> GMaxShadowDepthBufferSizeX(2048);
TRHIGlobal<int32> GMaxShadowDepthBufferSizeY(2048);
TRHIGlobal<int32> GMaxTextureDimensions(2048);
TRHIGlobal<int64> GMaxBufferDimensions(2<<27);
TRHIGlobal<int64> GMaxComputeSharedMemory(1<<15);
TRHIGlobal<int32> GMaxVolumeTextureDimensions(2048);
TRHIGlobal<int32> GMaxCubeTextureDimensions(2048);
TRHIGlobal<int32> GMaxWorkGroupInvocations(1024);
bool GRHISupportsRWTextureBuffers = true;
bool GRHISupportsVRS = false;
bool GRHISupportsLateVRSUpdate = false;
int32 GMaxTextureArrayLayers = 256;
int32 GMaxTextureSamplers = 16;
bool GUsingNullRHI = false;
int32 GDrawUPVertexCheckCount = MAX_int32;
int32 GDrawUPIndexCheckCount = MAX_int32;
bool GTriggerGPUProfile = false;
FString GGPUTraceFileName;
bool GRHISupportsTextureStreaming = false;
bool GSupportsDepthBoundsTest = false;
bool GSupportsEfficientAsyncCompute = false;
bool GRHISupportsBaseVertexIndex = true;
bool GRHISupportsFirstInstance = false;
bool GRHISupportsDynamicResolution = false;
bool GRHISupportsRayTracing = false;
bool GRHISupportsRayTracingPSOAdditions = false;
bool GRHISupportsRayTracingAsyncBuildAccelerationStructure = false;
bool GRHISupportsRayTracingAMDHitToken = false;
bool GRHISupportsWaveOperations = false;
int32 GRHIMinimumWaveSize = 4; // Minimum supported value in SM 6.0
int32 GRHIMaximumWaveSize = 128; // Maximum supported value in SM 6.0
bool GRHISupportsRHIThread = false;
bool GRHISupportsRHIOnTaskThread = false;
bool GRHISupportsParallelRHIExecute = false;
bool GSupportsParallelOcclusionQueries = false;
bool GSupportsTransientResourceAliasing = false;
bool GRHIRequiresRenderTargetForPixelShaderUAVs = false;
bool GRHISupportsUAVFormatAliasing = false;
bool GRHISupportsDirectGPUMemoryLock = false;

bool GRHISupportsMSAADepthSampleAccess = false;
bool GRHISupportsResolveCubemapFaces = false;

bool GRHISupportsBackBufferWithCustomDepthStencil = true;

bool GRHIIsHDREnabled = false;
bool GRHISupportsHDROutput = false;

bool GRHIVariableRateShadingEnabled = true;
bool GRHIAttachmentVariableRateShadingEnabled = true;
bool GRHISupportsPipelineVariableRateShading = false;
bool GRHISupportsAttachmentVariableRateShading = false;
bool GRHISupportsComplexVariableRateShadingCombinerOps = false;
bool GRHISupportsVariableRateShadingAttachmentArrayTextures = false;
int32 GRHIVariableRateShadingImageTileMaxWidth = 0;
int32 GRHIVariableRateShadingImageTileMaxHeight = 0;
int32 GRHIVariableRateShadingImageTileMinWidth = 0;
int32 GRHIVariableRateShadingImageTileMinHeight = 0;
EVRSImageDataType GRHIVariableRateShadingImageDataType = VRSImage_NotSupported;
EPixelFormat GRHIVariableRateShadingImageFormat = PF_Unknown;
bool GRHISupportsLateVariableRateShadingUpdate = false;

EPixelFormat GRHIHDRDisplayOutputFormat = PF_FloatRGBA;

uint64 GRHIPresentCounter = 1;

bool GRHISupportsArrayIndexFromAnyShader = false;

bool GRHISupportsPipelineFileCache = false;

/** Whether we are profiling GPU hitches. */
bool GTriggerGPUHitchProfile = false;

bool GRHISupportsPixelShaderUAVs = true;

FVertexElementTypeSupportInfo GVertexElementTypeSupport;

RHI_API int32 volatile GCurrentTextureMemorySize = 0;
RHI_API int32 volatile GCurrentRendertargetMemorySize = 0;
RHI_API int64 GTexturePoolSize = 0 * 1024 * 1024;
RHI_API int32 GPoolSizeVRAMPercentage = 0;

RHI_API EShaderPlatform GShaderPlatformForFeatureLevel[ERHIFeatureLevel::Num] = {SP_NumPlatforms,SP_NumPlatforms,SP_NumPlatforms,SP_NumPlatforms};

// simple stats about draw calls. GNum is the previous frame and 
// GCurrent is the current frame.
// GCurrentNumDrawCallsRHIPtr points to the drawcall counter to increment
RHI_API int32 GCurrentNumDrawCallsRHI[MAX_NUM_GPUS] = {};
RHI_API int32 GNumDrawCallsRHI[MAX_NUM_GPUS] = {};
RHI_API int32(*GCurrentNumDrawCallsRHIPtr)[MAX_NUM_GPUS] = &GCurrentNumDrawCallsRHI;
RHI_API int32 GCurrentNumPrimitivesDrawnRHI[MAX_NUM_GPUS] = {};
RHI_API int32 GNumPrimitivesDrawnRHI[MAX_NUM_GPUS] = {};

RHI_API uint64 GRHITransitionPrivateData_SizeInBytes = 0;
RHI_API uint64 GRHITransitionPrivateData_AlignInBytes = 0;

ERHIAccess GRHITextureReadAccessMask = ERHIAccess::ReadOnlyMask;

/** Called once per frame only from within an RHI. */
void RHIPrivateBeginFrame()
{
	for (int32 GPUIndex = 0; GPUIndex < MAX_NUM_GPUS; GPUIndex++)
	{
		GNumDrawCallsRHI[GPUIndex] = GCurrentNumDrawCallsRHI[GPUIndex];
	}
	
#if CSV_PROFILER
	// Only copy the display counters every so many frames to keep things more stable.
	const int32 FramesUntilDisplayCopy = 30;
	static int32 FrameCount = 0;
	bool bCopyDisplayFrames = false;
	++FrameCount;
	if (FrameCount >= FramesUntilDisplayCopy)
	{
		bCopyDisplayFrames = true;
		FrameCount = 0;
	}

	for (int32 Index=0; Index<FDrawCallCategoryName::NumCategory; ++Index)
	{
		FDrawCallCategoryName* CategoryName = FDrawCallCategoryName::Array[Index];
		for (int32 GPUIndex = 0; GPUIndex < MAX_NUM_GPUS; GPUIndex++)
		{
			if (bCopyDisplayFrames)
			{
				FDrawCallCategoryName::DisplayCounts[Index][GPUIndex] = CategoryName->Counters[GPUIndex];
			}
			GNumDrawCallsRHI[GPUIndex] += CategoryName->Counters[GPUIndex];
		}
		// Multi-GPU support : CSV stats do not support MGPU yet
		FCsvProfiler::RecordCustomStat(CategoryName->Name, CSV_CATEGORY_INDEX(DrawCall), CategoryName->Counters[0], ECsvCustomStatOp::Set);
		for (int32 GPUIndex = 0; GPUIndex < MAX_NUM_GPUS; GPUIndex++)
		{
			CategoryName->Counters[GPUIndex] = 0;
		}
	}
#endif

	for (int32 GPUIndex = 0; GPUIndex < MAX_NUM_GPUS; GPUIndex++)
	{
		GNumPrimitivesDrawnRHI[GPUIndex] = GCurrentNumPrimitivesDrawnRHI[GPUIndex];
	}
	// Multi-GPU support : CSV stats do not support MGPU yet
	CSV_CUSTOM_STAT(RHI, DrawCalls, GNumDrawCallsRHI[0], ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(RHI, PrimitivesDrawn, GNumPrimitivesDrawnRHI[0], ECsvCustomStatOp::Set);
	for (int32 GPUIndex = 0; GPUIndex < MAX_NUM_GPUS; GPUIndex++)
	{
		GCurrentNumDrawCallsRHI[GPUIndex] = GCurrentNumPrimitivesDrawnRHI[GPUIndex] = 0;
	}
}

/** Whether to initialize 3D textures using a bulk data (or through a mip update if false). */
RHI_API bool GUseTexture3DBulkDataRHI = false;

//
// The current shader platform.
//

RHI_API EShaderPlatform GMaxRHIShaderPlatform = SP_PCD3D_SM5;

/** The maximum feature level supported on this machine */
RHI_API ERHIFeatureLevel::Type GMaxRHIFeatureLevel = ERHIFeatureLevel::SM5;

FName FeatureLevelNames[] = 
{
	FName(TEXT("ES2")),
	FName(TEXT("ES3_1")),
	FName(TEXT("SM4_REMOVED")),
	FName(TEXT("SM5")),
};

static_assert(UE_ARRAY_COUNT(FeatureLevelNames) == ERHIFeatureLevel::Num, "Missing entry from feature level names.");

RHI_API bool GetFeatureLevelFromName(FName Name, ERHIFeatureLevel::Type& OutFeatureLevel)
{
	for (int32 NameIndex = 0; NameIndex < UE_ARRAY_COUNT(FeatureLevelNames); NameIndex++)
	{
		if (FeatureLevelNames[NameIndex] == Name)
		{
			OutFeatureLevel = (ERHIFeatureLevel::Type)NameIndex;
			return true;
		}
	}

	OutFeatureLevel = ERHIFeatureLevel::Num;
	return false;
}

RHI_API void GetFeatureLevelName(ERHIFeatureLevel::Type InFeatureLevel, FString& OutName)
{
	check(InFeatureLevel < UE_ARRAY_COUNT(FeatureLevelNames));
	if (InFeatureLevel < UE_ARRAY_COUNT(FeatureLevelNames))
	{
		FeatureLevelNames[(int32)InFeatureLevel].ToString(OutName);
	}
	else
	{
		OutName = TEXT("InvalidFeatureLevel");
	}	
}

static FName InvalidFeatureLevelName(TEXT("InvalidFeatureLevel"));
RHI_API void GetFeatureLevelName(ERHIFeatureLevel::Type InFeatureLevel, FName& OutName)
{
	check(InFeatureLevel < UE_ARRAY_COUNT(FeatureLevelNames));
	if (InFeatureLevel < UE_ARRAY_COUNT(FeatureLevelNames))
	{
		OutName = FeatureLevelNames[(int32)InFeatureLevel];
	}
	else
	{
		
		OutName = InvalidFeatureLevelName;
	}
}

FName ShadingPathNames[] =
{
	FName(TEXT("Deferred")),
	FName(TEXT("Forward")),
	FName(TEXT("Mobile")),
};

static_assert(UE_ARRAY_COUNT(ShadingPathNames) == ERHIShadingPath::Num, "Missing entry from shading path names.");

RHI_API bool GetShadingPathFromName(FName Name, ERHIShadingPath::Type& OutShadingPath)
{
	for (int32 NameIndex = 0; NameIndex < UE_ARRAY_COUNT(ShadingPathNames); NameIndex++)
	{
		if (ShadingPathNames[NameIndex] == Name)
		{
			OutShadingPath = (ERHIShadingPath::Type)NameIndex;
			return true;
		}
	}

	OutShadingPath = ERHIShadingPath::Num;
	return false;
}

RHI_API void GetShadingPathName(ERHIShadingPath::Type InShadingPath, FString& OutName)
{
	check(InShadingPath < UE_ARRAY_COUNT(ShadingPathNames));
	if (InShadingPath < UE_ARRAY_COUNT(ShadingPathNames))
	{
		ShadingPathNames[(int32)InShadingPath].ToString(OutName);
	}
	else
	{
		OutName = TEXT("InvalidShadingPath");
	}
}

static FName InvalidShadingPathName(TEXT("InvalidShadingPath"));
RHI_API void GetShadingPathName(ERHIShadingPath::Type InShadingPath, FName& OutName)
{
	check(InShadingPath < UE_ARRAY_COUNT(ShadingPathNames));
	if (InShadingPath < UE_ARRAY_COUNT(ShadingPathNames))
	{
		OutName = ShadingPathNames[(int32)InShadingPath];
	}
	else
	{

		OutName = InvalidShadingPathName;
	}
}

static FName NAME_PLATFORM_WINDOWS(TEXT("Windows"));
static FName NAME_PLATFORM_XBOXONE(TEXT("XboxOne"));
static FName NAME_PLATFORM_ANDROID(TEXT("Android"));
static FName NAME_PLATFORM_IOS(TEXT("IOS"));
static FName NAME_PLATFORM_MAC(TEXT("Mac"));
static FName NAME_PLATFORM_TVOS(TEXT("TVOS"));
static FName NAME_PLATFORM_LUMIN(TEXT("Lumin"));

// @todo platplug: This is still here, only being used now by UMaterialShaderQualitySettings::GetOrCreatePlatformSettings
// since I have moved the other uses to FindTargetPlatformWithSupport
// But I'd like to delete it anyway!
FName ShaderPlatformToPlatformName(EShaderPlatform Platform)
{
	switch (Platform)
	{
	case SP_PCD3D_SM5:
	case SP_PCD3D_ES3_1:
	case SP_OPENGL_PCES3_1:
	case SP_VULKAN_PCES3_1:
	case SP_VULKAN_SM5:
		return NAME_PLATFORM_WINDOWS;
	case SP_VULKAN_ES3_1_ANDROID:
	case SP_VULKAN_SM5_ANDROID:
	case SP_OPENGL_ES3_1_ANDROID:
		return NAME_PLATFORM_ANDROID;
	case SP_METAL:
	case SP_METAL_MRT:
		return NAME_PLATFORM_IOS;
	case SP_METAL_SM5:
	case SP_METAL_SM5_NOTESS:
	case SP_METAL_MACES3_1:
	case SP_METAL_MRT_MAC:
		return NAME_PLATFORM_MAC;
	case SP_VULKAN_SM5_LUMIN:
	case SP_VULKAN_ES3_1_LUMIN:
		return NAME_PLATFORM_LUMIN;
	case SP_METAL_TVOS:
	case SP_METAL_MRT_TVOS:
		return NAME_PLATFORM_TVOS;


	default:
		if (FStaticShaderPlatformNames::IsStaticPlatform(Platform))
		{
			return FStaticShaderPlatformNames::Get().GetPlatformName(Platform);
		}
		else
		{
			return NAME_None;
		}
	}
}

FName LegacyShaderPlatformToShaderFormat(EShaderPlatform Platform)
{
	return ShaderPlatformToShaderFormatName(Platform);
}

EShaderPlatform ShaderFormatToLegacyShaderPlatform(FName ShaderFormat)
{
	return ShaderFormatNameToShaderPlatform(ShaderFormat);
}

RHI_API bool IsRHIDeviceAMD()
{
	check(GRHIVendorId != 0);
	// AMD's drivers tested on July 11 2013 have hitching problems with async resource streaming, setting single threaded for now until fixed.
	return GRHIVendorId == 0x1002;
}

RHI_API bool IsRHIDeviceIntel()
{
	check(GRHIVendorId != 0);
	// Intel GPUs are integrated and use both DedicatedVideoMemory and SharedSystemMemory.
	return GRHIVendorId == 0x8086;
}

RHI_API bool IsRHIDeviceNVIDIA()
{
	check(GRHIVendorId != 0);
	// NVIDIA GPUs are discrete and use DedicatedVideoMemory only.
	return GRHIVendorId == 0x10DE;
}

RHI_API const TCHAR* RHIVendorIdToString()
{
	switch (GRHIVendorId)
	{
	case 0x1002: return TEXT("AMD");
	case 0x1010: return TEXT("ImgTec");
	case 0x10DE: return TEXT("NVIDIA");
	case 0x13B5: return TEXT("ARM");
	case 0x5143: return TEXT("Qualcomm");
	case 0x8086: return TEXT("Intel");
	default: return TEXT("Unknown");
	}
}

RHI_API const TCHAR* RHIVendorIdToString(EGpuVendorId VendorId)
{
	switch (VendorId)
	{
	case EGpuVendorId::Amd: return TEXT("AMD");
	case EGpuVendorId::ImgTec: return TEXT("ImgTec");
	case EGpuVendorId::Nvidia: return TEXT("NVIDIA");
	case EGpuVendorId::Arm: return TEXT("ARM");
	case EGpuVendorId::Qualcomm: return TEXT("Qualcomm");
	case EGpuVendorId::Intel: return TEXT("Intel");
	case EGpuVendorId::NotQueried: return TEXT("Not Queried");
	default:
		break;
	}

	return TEXT("Unknown");
}

RHI_API uint32 RHIGetShaderLanguageVersion(const FStaticShaderPlatform Platform)
{
	uint32 Version = 0;
	if (IsMetalPlatform(Platform))
	{
		if (IsPCPlatform(Platform))
		{
			static int32 MaxShaderVersion = -1;
			if (MaxShaderVersion < 0)
			{
				MaxShaderVersion = 2;
				int32 MinShaderVersion = 3;
				if(!GConfig->GetInt(TEXT("/Script/MacTargetPlatform.MacTargetSettings"), TEXT("MaxShaderLanguageVersion"), MaxShaderVersion, GEngineIni))
				{
					MaxShaderVersion = 4;
				}
				MaxShaderVersion = FMath::Max(MinShaderVersion, MaxShaderVersion);
			}
			Version = (uint32)MaxShaderVersion;
		}
		else
		{
			static int32 MaxShaderVersion = -1;
			if (MaxShaderVersion < 0)
			{
				MaxShaderVersion = 2;
				int32 MinShaderVersion = 2;
				if(!GConfig->GetInt(TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings"), TEXT("MaxShaderLanguageVersion"), MaxShaderVersion, GEngineIni))
				{
					MaxShaderVersion = 0;
				}
                
                // If we are using Mobile desktop rendering, we need a minimum of Metal 2.1
                if(IsMetalSM5Platform(Platform))
                {
                    MinShaderVersion = 4;
                }
                
				MaxShaderVersion = FMath::Max(MinShaderVersion, MaxShaderVersion);
			}
			Version = (uint32)MaxShaderVersion;
		}
	}
	return Version;
}

RHI_API bool RHISupportsTessellation(const FStaticShaderPlatform Platform)
{
	if (FDataDrivenShaderPlatformInfo::GetSupportsTessellation(Platform))
	{
		return true;
	}

	if (IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5))
	{
		return (Platform == SP_PCD3D_SM5) || (Platform == SP_METAL_SM5) || (IsVulkanSM5Platform(Platform));
	}
	return false;
}

RHI_API bool RHISupportsIndexBufferUAVs(const FStaticShaderPlatform Platform)
{
	return Platform == SP_PCD3D_SM5 || IsVulkanPlatform(Platform) || IsMetalSM5Platform(Platform)
		|| FDataDrivenShaderPlatformInfo::GetSupportsIndexBufferUAVs(Platform);
}


static ERHIFeatureLevel::Type GRHIMobilePreviewFeatureLevel = ERHIFeatureLevel::Num;
RHI_API void RHISetMobilePreviewFeatureLevel(ERHIFeatureLevel::Type MobilePreviewFeatureLevel)
{
	check(GRHIMobilePreviewFeatureLevel == ERHIFeatureLevel::Num);
	check(!GIsEditor);
	GRHIMobilePreviewFeatureLevel = MobilePreviewFeatureLevel;
}

bool RHIGetPreviewFeatureLevel(ERHIFeatureLevel::Type& PreviewFeatureLevelOUT)
{
	static bool bForceFeatureLevelES3_1 = !GIsEditor && (FParse::Param(FCommandLine::Get(), TEXT("FeatureLevelES31")) || FParse::Param(FCommandLine::Get(), TEXT("FeatureLevelES3_1")));

	if (bForceFeatureLevelES3_1)
	{
		PreviewFeatureLevelOUT = ERHIFeatureLevel::ES3_1;
	}
	else if (!GIsEditor && GRHIMobilePreviewFeatureLevel != ERHIFeatureLevel::Num)
	{
		PreviewFeatureLevelOUT = GRHIMobilePreviewFeatureLevel;
	}
	else
	{
		return false;
	}
	return true;
}

 RHI_API EPixelFormat RHIPreferredPixelFormatHint(EPixelFormat PreferredPixelFormat)
{
	if (GDynamicRHI)
	{
		return GDynamicRHI->RHIPreferredPixelFormatHint(PreferredPixelFormat);
	}
	return PreferredPixelFormat;
}

RHI_API int32 RHIGetPreferredClearUAVRectPSResourceType(const FStaticShaderPlatform Platform)
{
	if (IsMetalPlatform(Platform))
	{
		static constexpr uint32 METAL_TEXTUREBUFFER_SHADER_LANGUAGE_VERSION = 4;
		if (METAL_TEXTUREBUFFER_SHADER_LANGUAGE_VERSION <= RHIGetShaderLanguageVersion(Platform))
		{
			return 0; // BUFFER
		}
	}
	return 1; // TEXTURE_2D
}

void FRHIRenderPassInfo::ConvertToRenderTargetsInfo(FRHISetRenderTargetsInfo& OutRTInfo) const
{
	for (int32 Index = 0; Index < MaxSimultaneousRenderTargets; ++Index)
	{
		if (!ColorRenderTargets[Index].RenderTarget)
		{
			break;
		}

		OutRTInfo.ColorRenderTarget[Index].Texture = ColorRenderTargets[Index].RenderTarget;
		ERenderTargetLoadAction LoadAction = GetLoadAction(ColorRenderTargets[Index].Action);
		OutRTInfo.ColorRenderTarget[Index].LoadAction = LoadAction;
		OutRTInfo.ColorRenderTarget[Index].StoreAction = GetStoreAction(ColorRenderTargets[Index].Action);
		OutRTInfo.ColorRenderTarget[Index].ArraySliceIndex = ColorRenderTargets[Index].ArraySlice;
		OutRTInfo.ColorRenderTarget[Index].MipIndex = ColorRenderTargets[Index].MipIndex;
		++OutRTInfo.NumColorRenderTargets;

		OutRTInfo.bClearColor |= (LoadAction == ERenderTargetLoadAction::EClear);

		ensure(!OutRTInfo.bHasResolveAttachments || ColorRenderTargets[Index].ResolveTarget);
		if (ColorRenderTargets[Index].ResolveTarget)
		{
			OutRTInfo.bHasResolveAttachments = true;
			OutRTInfo.ColorResolveRenderTarget[Index] = OutRTInfo.ColorRenderTarget[Index];
			OutRTInfo.ColorResolveRenderTarget[Index].Texture = ColorRenderTargets[Index].ResolveTarget;
		}
	}

	ERenderTargetActions DepthActions = GetDepthActions(DepthStencilRenderTarget.Action);
	ERenderTargetActions StencilActions = GetStencilActions(DepthStencilRenderTarget.Action);
	ERenderTargetLoadAction DepthLoadAction = GetLoadAction(DepthActions);
	ERenderTargetStoreAction DepthStoreAction = GetStoreAction(DepthActions);
	ERenderTargetLoadAction StencilLoadAction = GetLoadAction(StencilActions);
	ERenderTargetStoreAction StencilStoreAction = GetStoreAction(StencilActions);

	OutRTInfo.DepthStencilRenderTarget = FRHIDepthRenderTargetView(DepthStencilRenderTarget.DepthStencilTarget,
		DepthLoadAction,
		GetStoreAction(DepthActions),
		StencilLoadAction,
		GetStoreAction(StencilActions),
		DepthStencilRenderTarget.ExclusiveDepthStencil);
	OutRTInfo.bClearDepth = (DepthLoadAction == ERenderTargetLoadAction::EClear);
	OutRTInfo.bClearStencil = (StencilLoadAction == ERenderTargetLoadAction::EClear);

	OutRTInfo.ShadingRateTexture = ShadingRateTexture;
	OutRTInfo.ShadingRateTextureCombiner = ShadingRateTextureCombiner;
	OutRTInfo.MultiViewCount = MultiViewCount;
}

void FRHIRenderPassInfo::OnVerifyNumUAVsFailed(int32 InNumUAVs)
{
	bTooManyUAVs = true;
	UE_LOG(LogRHI, Warning, TEXT("NumUAVs is %d which is greater the max %d. Trailing UAVs will be dropped"), InNumUAVs, MaxSimultaneousUAVs);
	// Trigger an ensure to get callstack in dev builds
	ensure(InNumUAVs <= MaxSimultaneousUAVs);
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
void FRHIRenderPassInfo::Validate() const
{
	int32 NumSamples = -1;	// -1 means nothing found yet
	int32 ColorIndex = 0;
	for (; ColorIndex < MaxSimultaneousRenderTargets; ++ColorIndex)
	{
		const FColorEntry& Entry = ColorRenderTargets[ColorIndex];
		if (Entry.RenderTarget)
		{
			// Ensure NumSamples matches amongst all color RTs
			if (NumSamples == -1)
			{
				NumSamples = Entry.RenderTarget->GetNumSamples();
			}
			else
			{
				ensure(Entry.RenderTarget->GetNumSamples() == NumSamples);
			}

			ERenderTargetStoreAction Store = GetStoreAction(Entry.Action);
			// Don't try to resolve a non-msaa
			ensure(Store != ERenderTargetStoreAction::EMultisampleResolve || Entry.RenderTarget->GetNumSamples() > 1);
			// Don't resolve to null
			ensure(Store != ERenderTargetStoreAction::EMultisampleResolve || Entry.ResolveTarget);

			if (Entry.ResolveTarget)
			{
				//ensure(Store == ERenderTargetStoreAction::EMultisampleResolve);
			}
		}
		else
		{
			break;
		}
	}

	int32 NumColorRenderTargets = ColorIndex;
	for (; ColorIndex < MaxSimultaneousRenderTargets; ++ColorIndex)
	{
		// Gap in the sequence of valid render targets (ie RT0, null, RT2, ...)
		ensureMsgf(!ColorRenderTargets[ColorIndex].RenderTarget, TEXT("Missing color render target on slot %d"), ColorIndex - 1);
	}

	if (DepthStencilRenderTarget.DepthStencilTarget)
	{
		// Ensure NumSamples matches with color RT
		if (NumSamples != -1)
		{
			ensure(DepthStencilRenderTarget.DepthStencilTarget->GetNumSamples() == NumSamples);
		}
		ERenderTargetStoreAction DepthStore = GetStoreAction(GetDepthActions(DepthStencilRenderTarget.Action));
		ERenderTargetStoreAction StencilStore = GetStoreAction(GetStencilActions(DepthStencilRenderTarget.Action));
		bool bIsMSAAResolve = (DepthStore == ERenderTargetStoreAction::EMultisampleResolve) || (StencilStore == ERenderTargetStoreAction::EMultisampleResolve);
		// Don't try to resolve a non-msaa
		ensure(!bIsMSAAResolve || DepthStencilRenderTarget.DepthStencilTarget->GetNumSamples() > 1);
		// Don't resolve to null
		//ensure(DepthStencilRenderTarget.ResolveTarget || DepthStore != ERenderTargetStoreAction::EStore);

		// Don't write to depth if read-only
		//ensure(DepthStencilRenderTarget.ExclusiveDepthStencil.IsDepthWrite() || DepthStore != ERenderTargetStoreAction::EStore);
		// This is not true for stencil. VK and Metal specify that the DontCare store action MAY leave the attachment in an undefined state.
		/*ensure(DepthStencilRenderTarget.ExclusiveDepthStencil.IsStencilWrite() || StencilStore != ERenderTargetStoreAction::EStore);*/

		// If we have a depthstencil target we MUST Store it or it will be undefined after rendering.
		if (DepthStencilRenderTarget.DepthStencilTarget->GetFormat() != PF_D24)
		{
			// If this is DepthStencil we must store it out unless we are absolutely sure it will never be used again.
			// it is valid to use a depthbuffer for performance and not need the results later.
			//ensure(StencilStore == ERenderTargetStoreAction::EStore);
		}

		if (DepthStencilRenderTarget.ExclusiveDepthStencil.IsDepthWrite())
		{
			// this check is incorrect for mobile, depth/stencil is intermediate and we don't want to store it to main memory
			//ensure(DepthStore == ERenderTargetStoreAction::EStore);
		}

		if (DepthStencilRenderTarget.ExclusiveDepthStencil.IsStencilWrite())
		{
			// this check is incorrect for mobile, depth/stencil is intermediate and we don't want to store it to main memory
			//ensure(StencilStore == ERenderTargetStoreAction::EStore);
		}
		
		if (SubpassHint == ESubpassHint::DepthReadSubpass)
		{
			// for depth read sub-pass
			// 1. render pass must have depth target
			// 2. depth target must support InputAttachement
			ensure((DepthStencilRenderTarget.DepthStencilTarget->GetFlags() & TexCreate_InputAttachmentRead) != 0);
		}
	}
	else
	{
		ensure(DepthStencilRenderTarget.Action == EDepthStencilTargetActions::DontLoad_DontStore);
		ensure(DepthStencilRenderTarget.ExclusiveDepthStencil == FExclusiveDepthStencil::DepthNop_StencilNop);
		ensure(SubpassHint != ESubpassHint::DepthReadSubpass);
	}
}
#endif

static FRHIPanicEvent RHIPanicEvent;
FRHIPanicEvent& RHIGetPanicDelegate()
{
	return RHIPanicEvent;
}

#include "Misc/DataDrivenPlatformInfoRegistry.h"

FString LexToString(EShaderPlatform Platform, bool bError)
{
	switch (Platform)
	{
	case SP_PCD3D_SM5: return TEXT("PCD3D_SM5");
	case SP_PCD3D_ES3_1: return TEXT("PCD3D_ES3_1");
	case SP_OPENGL_PCES3_1: return TEXT("OPENGL_PCES3_1");
	case SP_OPENGL_ES3_1_ANDROID: return TEXT("OPENGL_ES3_1_ANDROID");
	case SP_METAL: return TEXT("METAL");
	case SP_METAL_MRT: return TEXT("METAL_MRT");
	case SP_METAL_TVOS: return TEXT("METAL_TVOS");
	case SP_METAL_MRT_TVOS: return TEXT("METAL_MRT_TVOS");
	case SP_METAL_MRT_MAC: return TEXT("METAL_MRT_MAC");
	case SP_METAL_SM5: return TEXT("METAL_SM5");
	case SP_METAL_SM5_NOTESS: return TEXT("METAL_SM5_NOTESS");
	case SP_METAL_MACES3_1: return TEXT("METAL_MACES3_1");
	case SP_VULKAN_ES3_1_ANDROID: return TEXT("VULKAN_ES3_1_ANDROID");
	case SP_VULKAN_ES3_1_LUMIN: return TEXT("VULKAN_ES3_1_LUMIN");
	case SP_VULKAN_PCES3_1: return TEXT("VULKAN_PCES3_1");
	case SP_VULKAN_SM5: return TEXT("VULKAN_SM5");
	case SP_VULKAN_SM5_LUMIN: return TEXT("VULKAN_SM5_LUMIN");
	case SP_VULKAN_SM5_ANDROID: return TEXT("VULKAN_SM5_ANDROID");

	default:
		if (FStaticShaderPlatformNames::IsStaticPlatform(Platform))
		{
			return FStaticShaderPlatformNames::Get().GetShaderPlatform(Platform).ToString();
		}
		else
		{
			checkf(!bError, TEXT("Unknown or removed EShaderPlatform %d!"), (int32)Platform);
			return TEXT("");
		}
	}
}

FString LexToString(EShaderPlatform Platform)
{
	bool bError = true;
	return LexToString(Platform, bError);
}

void LexFromString(EShaderPlatform& Value, const TCHAR* String)
{
	Value = EShaderPlatform::SP_NumPlatforms;

	for (uint8 i = 0; i < (uint8)EShaderPlatform::SP_NumPlatforms; ++i)
	{
		if (LexToString((EShaderPlatform)i, false).Equals(String))
		{
			Value = (EShaderPlatform)i;
			return;
		}
	}
}

FString LexToString(ERHIFeatureLevel::Type Level)
{
	switch (Level)
	{
		case ERHIFeatureLevel::ES2_REMOVED:
			return TEXT("ES2_REMOVED");
		case ERHIFeatureLevel::ES3_1:
			return TEXT("ES3_1");
		case ERHIFeatureLevel::SM4_REMOVED:
			return TEXT("SM4_REMOVED");
		case ERHIFeatureLevel::SM5:
			return TEXT("SM5");
		default:
			break;
	}
	return TEXT("UnknownFeatureLevel");
}

const FName LANGUAGE_D3D("D3D");
const FName LANGUAGE_Metal("Metal");
const FName LANGUAGE_OpenGL("OpenGL");
const FName LANGUAGE_Vulkan("Vulkan");
const FName LANGUAGE_Sony("Sony");
const FName LANGUAGE_Nintendo("Nintendo");

RHI_API FGenericDataDrivenShaderPlatformInfo FGenericDataDrivenShaderPlatformInfo::Infos[SP_NumPlatforms];

// Gets a string from a section, or empty string if it didn't exist
static inline FString GetSectionString(const FConfigSection& Section, FName Key)
{
	return Section.FindRef(Key).GetValue();
}

// Gets a bool from a section.  It returns the original value if the setting does not exist
static inline bool GetSectionBool(const FConfigSection& Section, FName Key, bool OriginalValue)
{
	const FConfigValue* ConfigValue = Section.Find(Key);
	if (ConfigValue != nullptr)
	{
		return FCString::ToBool(*ConfigValue->GetValue());
	}
	else
	{
		return OriginalValue;
	}
}

// Gets an integer from a section.  It returns the original value if the setting does not exist
static inline uint32 GetSectionUint(const FConfigSection& Section, FName Key, uint32 OriginalValue)
{
	const FConfigValue* ConfigValue = Section.Find(Key);
	if (ConfigValue != nullptr)
	{
		return (uint32)FCString::Atoi(*ConfigValue->GetValue());
	}
	else
	{
		return OriginalValue;
	}
}

void FGenericDataDrivenShaderPlatformInfo::SetDefaultValues()
{
	MaxFeatureLevel = ERHIFeatureLevel::Num;
	bSupportsMSAA = true;

	bNeedsToSwitchVerticalAxisOnMobileOpenGL = true;
	bSupportsDOFHybridScattering = true;
	bSupportsHZBOcclusion = true;
	bSupportsWaterIndirectDraw = true;
	bSupportsAsyncPipelineCompilation = true;
}

void FGenericDataDrivenShaderPlatformInfo::ParseDataDrivenShaderInfo(const FConfigSection& Section, FGenericDataDrivenShaderPlatformInfo& Info)
{
	Info.Language = *GetSectionString(Section, "Language");
	GetFeatureLevelFromName(*GetSectionString(Section, "MaxFeatureLevel"), Info.MaxFeatureLevel);

#define GET_SECTION_BOOL_HELPER(SettingName)	\
	Info.SettingName = GetSectionBool(Section, #SettingName, Info.SettingName)
#define GET_SECTION_INT_HELPER(SettingName)	\
	Info.SettingName = GetSectionUint(Section, #SettingName, Info.SettingName)

	GET_SECTION_BOOL_HELPER(bIsMobile);
	GET_SECTION_BOOL_HELPER(bIsMetalMRT);
	GET_SECTION_BOOL_HELPER(bIsPC);
	GET_SECTION_BOOL_HELPER(bIsConsole);
	GET_SECTION_BOOL_HELPER(bIsAndroidOpenGLES);
	GET_SECTION_BOOL_HELPER(bSupportsMobileMultiView);
	GET_SECTION_BOOL_HELPER(bSupportsVolumeTextureCompression);
	GET_SECTION_BOOL_HELPER(bSupportsDistanceFields);
	GET_SECTION_BOOL_HELPER(bSupportsDiaphragmDOF);
	GET_SECTION_BOOL_HELPER(bSupportsRGBColorBuffer);
	GET_SECTION_BOOL_HELPER(bSupportsCapsuleShadows);
	GET_SECTION_BOOL_HELPER(bSupportsVolumetricFog);
	GET_SECTION_BOOL_HELPER(bSupportsIndexBufferUAVs);
	GET_SECTION_BOOL_HELPER(bSupportsInstancedStereo);
	GET_SECTION_BOOL_HELPER(bSupportsMultiView);
	GET_SECTION_BOOL_HELPER(bSupportsMSAA);
	GET_SECTION_BOOL_HELPER(bSupports4ComponentUAVReadWrite);
	GET_SECTION_BOOL_HELPER(bSupportsRenderTargetWriteMask);
	GET_SECTION_BOOL_HELPER(bSupportsRayTracing);
	GET_SECTION_BOOL_HELPER(bSupportsRayTracingIndirectInstanceData);
	GET_SECTION_BOOL_HELPER(bSupportsPathTracing);
	GET_SECTION_BOOL_HELPER(bSupportsGPUSkinCache);
	GET_SECTION_BOOL_HELPER(bSupportsByteBufferComputeShaders);
	GET_SECTION_BOOL_HELPER(bSupportsGPUScene);
	GET_SECTION_BOOL_HELPER(bSupportsPrimitiveShaders);
	GET_SECTION_BOOL_HELPER(bSupportsUInt64ImageAtomics);
	GET_SECTION_BOOL_HELPER(bSupportsTemporalHistoryUpscale);
	GET_SECTION_BOOL_HELPER(bSupportsRTIndexFromVS);
	GET_SECTION_BOOL_HELPER(bSupportsWaveOperations);
	GET_SECTION_BOOL_HELPER(bRequiresExplicit128bitRT);
	GET_SECTION_BOOL_HELPER(bSupportsGen5TemporalAA);
	GET_SECTION_BOOL_HELPER(bTargetsTiledGPU);
	GET_SECTION_BOOL_HELPER(bNeedsOfflineCompiler);
	GET_SECTION_BOOL_HELPER(bSupportsAnisotropicMaterials);
	GET_SECTION_BOOL_HELPER(bSupportsDualSourceBlending);
	GET_SECTION_BOOL_HELPER(bRequiresGeneratePrevTransformBuffer);
	GET_SECTION_BOOL_HELPER(bRequiresRenderTargetDuringRaster);
	GET_SECTION_BOOL_HELPER(bRequiresDisableForwardLocalLights);
	GET_SECTION_BOOL_HELPER(bCompileSignalProcessingPipeline);
	GET_SECTION_BOOL_HELPER(bSupportsTessellation);
	GET_SECTION_BOOL_HELPER(bSupportsPerPixelDBufferMask);
	GET_SECTION_BOOL_HELPER(bIsHlslcc);
	GET_SECTION_BOOL_HELPER(bSupportsVariableRateShading);
	GET_SECTION_INT_HELPER(NumberOfComputeThreads);

	GET_SECTION_BOOL_HELPER(bWaterUsesSimpleForwardShading);
	GET_SECTION_BOOL_HELPER(bNeedsToSwitchVerticalAxisOnMobileOpenGL);
	GET_SECTION_BOOL_HELPER(bSupportsHairStrandGeometry);
	GET_SECTION_BOOL_HELPER(bSupportsDOFHybridScattering);
	GET_SECTION_BOOL_HELPER(bNeedsExtraMobileFrames);
	GET_SECTION_BOOL_HELPER(bSupportsHZBOcclusion);
	GET_SECTION_BOOL_HELPER(bSupportsWaterIndirectDraw);
	GET_SECTION_BOOL_HELPER(bSupportsAsyncPipelineCompilation);
	GET_SECTION_BOOL_HELPER(bSupportsManualVertexFetch);
	GET_SECTION_BOOL_HELPER(bRequiresReverseCullingOnMobile);
	GET_SECTION_BOOL_HELPER(bOverrideFMaterial_NeedsGBufferEnabled);
	GET_SECTION_BOOL_HELPER(bSupportsMobileDistanceField);
#undef GET_SECTION_BOOL_HELPER
#undef GET_SECTION_INT_HELPER

#if WITH_EDITOR
	FTextStringHelper::ReadFromBuffer(*GetSectionString(Section, FName("FriendlyName")), Info.FriendlyName);
#endif
}

void FGenericDataDrivenShaderPlatformInfo::Initialize()
{
	// look for the standard DataDriven ini files
	int32 NumDDInfoFiles = FDataDrivenPlatformInfoRegistry::GetNumDataDrivenIniFiles();
	for (int32 Index = 0; Index < NumDDInfoFiles; Index++)
	{
		FConfigFile IniFile;
		FString PlatformName;

		FDataDrivenPlatformInfoRegistry::LoadDataDrivenIniFile(Index, IniFile, PlatformName);

		// now walk over the file, looking for ShaderPlatformInfo sections
		for (auto Section : IniFile)
		{
			if (Section.Key.StartsWith(TEXT("ShaderPlatform ")))
			{
				const FString& SectionName = Section.Key;

				EShaderPlatform ShaderPlatform;
				// get enum value for the string name
				LexFromString(ShaderPlatform, *SectionName.Mid(15));
				if (ShaderPlatform == EShaderPlatform::SP_NumPlatforms)
				{
					UE_LOG(LogRHI, Warning, TEXT("Found an unknown shader platform %s in a DataDriven ini file"), *SectionName.Mid(15));
					continue;
				}
				
				// at this point, we can start pulling information out
				ParseDataDrivenShaderInfo(Section.Value, Infos[ShaderPlatform]);	
				Infos[ShaderPlatform].bContainsValidPlatformInfo = true;
			}
		}
	}
}

//
//	Pixel format information.
//

FPixelFormatInfo	GPixelFormats[PF_MAX] =
{
	// Name						BlockSizeX	BlockSizeY	BlockSizeZ	BlockBytes	NumComponents	PlatformFormat	Supported		UnrealFormat

	{ TEXT("unknown"),			0,			0,			0,			0,			0,				0,				0,				PF_Unknown			},
	{ TEXT("A32B32G32R32F"),	1,			1,			1,			16,			4,				0,				1,				PF_A32B32G32R32F	},
	{ TEXT("B8G8R8A8"),			1,			1,			1,			4,			4,				0,				1,				PF_B8G8R8A8			},
	{ TEXT("G8"),				1,			1,			1,			1,			1,				0,				1,				PF_G8				},
	{ TEXT("G16"),				1,			1,			1,			2,			1,				0,				1,				PF_G16				},
	{ TEXT("DXT1"),				4,			4,			1,			8,			3,				0,				1,				PF_DXT1				},
	{ TEXT("DXT3"),				4,			4,			1,			16,			4,				0,				1,				PF_DXT3				},
	{ TEXT("DXT5"),				4,			4,			1,			16,			4,				0,				1,				PF_DXT5				},
	{ TEXT("UYVY"),				2,			1,			1,			4,			4,				0,				0,				PF_UYVY				},
	{ TEXT("FloatRGB"),			1,			1,			1,			4,			3,				0,				1,				PF_FloatRGB			},
	{ TEXT("FloatRGBA"),		1,			1,			1,			8,			4,				0,				1,				PF_FloatRGBA		},
	{ TEXT("DepthStencil"),		1,			1,			1,			4,			1,				0,				0,				PF_DepthStencil		},
	{ TEXT("ShadowDepth"),		1,			1,			1,			4,			1,				0,				0,				PF_ShadowDepth		},
	{ TEXT("R32_FLOAT"),		1,			1,			1,			4,			1,				0,				1,				PF_R32_FLOAT		},
	{ TEXT("G16R16"),			1,			1,			1,			4,			2,				0,				1,				PF_G16R16			},
	{ TEXT("G16R16F"),			1,			1,			1,			4,			2,				0,				1,				PF_G16R16F			},
	{ TEXT("G16R16F_FILTER"),	1,			1,			1,			4,			2,				0,				1,				PF_G16R16F_FILTER	},
	{ TEXT("G32R32F"),			1,			1,			1,			8,			2,				0,				1,				PF_G32R32F			},
	{ TEXT("A2B10G10R10"),      1,          1,          1,          4,          4,              0,              1,				PF_A2B10G10R10		},
	{ TEXT("A16B16G16R16"),		1,			1,			1,			8,			4,				0,				1,				PF_A16B16G16R16		},
	{ TEXT("D24"),				1,			1,			1,			4,			1,				0,				1,				PF_D24				},
	{ TEXT("PF_R16F"),			1,			1,			1,			2,			1,				0,				1,				PF_R16F				},
	{ TEXT("PF_R16F_FILTER"),	1,			1,			1,			2,			1,				0,				1,				PF_R16F_FILTER		},
	{ TEXT("BC5"),				4,			4,			1,			16,			2,				0,				1,				PF_BC5				},
	{ TEXT("V8U8"),				1,			1,			1,			2,			2,				0,				1,				PF_V8U8				},
	{ TEXT("A1"),				1,			1,			1,			1,			1,				0,				0,				PF_A1				},
	{ TEXT("FloatR11G11B10"),	1,			1,			1,			4,			3,				0,				0,				PF_FloatR11G11B10	},
	{ TEXT("A8"),				1,			1,			1,			1,			1,				0,				1,				PF_A8				},	
	{ TEXT("R32_UINT"),			1,			1,			1,			4,			1,				0,				1,				PF_R32_UINT			},
	{ TEXT("R32_SINT"),			1,			1,			1,			4,			1,				0,				1,				PF_R32_SINT			},

	// IOS Support
	{ TEXT("PVRTC2"),			8,			4,			1,			8,			4,				0,				0,				PF_PVRTC2			},
	{ TEXT("PVRTC4"),			4,			4,			1,			8,			4,				0,				0,				PF_PVRTC4			},

	{ TEXT("R16_UINT"),			1,			1,			1,			2,			1,				0,				1,				PF_R16_UINT			},
	{ TEXT("R16_SINT"),			1,			1,			1,			2,			1,				0,				1,				PF_R16_SINT			},
	{ TEXT("R16G16B16A16_UINT"),1,			1,			1,			8,			4,				0,				1,				PF_R16G16B16A16_UINT},
	{ TEXT("R16G16B16A16_SINT"),1,			1,			1,			8,			4,				0,				1,				PF_R16G16B16A16_SINT},
	{ TEXT("R5G6B5_UNORM"),     1,          1,          1,          2,          3,              0,              1,              PF_R5G6B5_UNORM		},
	{ TEXT("R8G8B8A8"),			1,			1,			1,			4,			4,				0,				1,				PF_R8G8B8A8			},
	{ TEXT("A8R8G8B8"),			1,			1,			1,			4,			4,				0,				1,				PF_A8R8G8B8			},
	{ TEXT("BC4"),				4,			4,			1,			8,			1,				0,				1,				PF_BC4				},
	{ TEXT("R8G8"),				1,			1,			1,			2,			2,				0,				1,				PF_R8G8				},

	{ TEXT("ATC_RGB"),			4,			4,			1,			8,			3,				0,				0,				PF_ATC_RGB			},
	{ TEXT("ATC_RGBA_E"),		4,			4,			1,			16,			4,				0,				0,				PF_ATC_RGBA_E		},
	{ TEXT("ATC_RGBA_I"),		4,			4,			1,			16,			4,				0,				0,				PF_ATC_RGBA_I		},
	{ TEXT("X24_G8"),			1,			1,			1,			1,			1,				0,				0,				PF_X24_G8			},
	{ TEXT("ETC1"),				4,			4,			1,			8,			3,				0,				0,				PF_ETC1				},
	{ TEXT("ETC2_RGB"),			4,			4,			1,			8,			3,				0,				0,				PF_ETC2_RGB			},
	{ TEXT("ETC2_RGBA"),		4,			4,			1,			16,			4,				0,				0,				PF_ETC2_RGBA		},
	{ TEXT("PF_R32G32B32A32_UINT"),1,		1,			1,			16,			4,				0,				1,				PF_R32G32B32A32_UINT},
	{ TEXT("PF_R16G16_UINT"),	1,			1,			1,			4,			4,				0,				1,				PF_R16G16_UINT},

	// ASTC support
	{ TEXT("ASTC_4x4"),			4,			4,			1,			16,			4,				0,				0,				PF_ASTC_4x4			},
	{ TEXT("ASTC_6x6"),			6,			6,			1,			16,			4,				0,				0,				PF_ASTC_6x6			},
	{ TEXT("ASTC_8x8"),			8,			8,			1,			16,			4,				0,				0,				PF_ASTC_8x8			},
	{ TEXT("ASTC_10x10"),		10,			10,			1,			16,			4,				0,				0,				PF_ASTC_10x10		},
	{ TEXT("ASTC_12x12"),		12,			12,			1,			16,			4,				0,				0,				PF_ASTC_12x12		},

	{ TEXT("BC6H"),				4,			4,			1,			16,			3,				0,				1,				PF_BC6H				},
	{ TEXT("BC7"),				4,			4,			1,			16,			4,				0,				1,				PF_BC7				},
	{ TEXT("R8_UINT"),			1,			1,			1,			1,			1,				0,				1,				PF_R8_UINT			},
	{ TEXT("L8"),				1,			1,			1,			1,			1,				0,				0,				PF_L8				},
	{ TEXT("XGXR8"),			1,			1,			1,			4,			4,				0,				1,				PF_XGXR8 			},
	{ TEXT("R8G8B8A8_UINT"),	1,			1,			1,			4,			4,				0,				1,				PF_R8G8B8A8_UINT	},
	{ TEXT("R8G8B8A8_SNORM"),	1,			1,			1,			4,			4,				0,				1,				PF_R8G8B8A8_SNORM	},

	{ TEXT("R16G16B16A16_UINT"),1,			1,			1,			8,			4,				0,				1,				PF_R16G16B16A16_UNORM },
	{ TEXT("R16G16B16A16_SINT"),1,			1,			1,			8,			4,				0,				1,				PF_R16G16B16A16_SNORM },
	{ TEXT("PLATFORM_HDR_0"),	0,			0,			0,			0,			0,				0,				0,				PF_PLATFORM_HDR_0   },
	{ TEXT("PLATFORM_HDR_1"),	0,			0,			0,			0,			0,				0,				0,				PF_PLATFORM_HDR_1   },
	{ TEXT("PLATFORM_HDR_2"),	0,			0,			0,			0,			0,				0,				0,				PF_PLATFORM_HDR_2   },

	// NV12 contains 2 textures: R8 luminance plane followed by R8G8 1/4 size chrominance plane.
	// BlockSize/BlockBytes/NumComponents values don't make much sense for this format, so set them all to one.
	{ TEXT("NV12"),				1,			1,			1,			1,			1,				0,				0,				PF_NV12             },

	{ TEXT("PF_R32G32_UINT"),   1,   		1,			1,			8,			2,				0,				1,				PF_R32G32_UINT      },

	{ TEXT("PF_ETC2_R11_EAC"),  4,   		4,			1,			8,			1,				0,				0,				PF_ETC2_R11_EAC     },
	{ TEXT("PF_ETC2_RG11_EAC"), 4,   		4,			1,			16,			2,				0,				0,				PF_ETC2_RG11_EAC    },
	{ TEXT("R8"),				1,			1,			1,			1,			1,				0,				1,				PF_R8				},
};

static struct FValidatePixelFormats
{
	FValidatePixelFormats()
	{
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(GPixelFormats); ++Index)
		{
			// Make sure GPixelFormats has an entry for every unreal format
			checkf((EPixelFormat)Index == GPixelFormats[Index].UnrealFormat, TEXT("Missing entry for EPixelFormat %d"), (int32)Index);
		}
	}
} ValidatePixelFormats;

//
//	CalculateImageBytes
//

SIZE_T CalculateImageBytes(uint32 SizeX,uint32 SizeY,uint32 SizeZ,uint8 Format)
{
	if ( Format == PF_A1 )
	{
		// The number of bytes needed to store all 1 bit pixels in a line is the width of the image divided by the number of bits in a byte
		uint32 BytesPerLine = SizeX / 8;
		// The number of actual bytes in a 1 bit image is the bytes per line of pixels times the number of lines
		return sizeof(uint8) * BytesPerLine * SizeY;
	}
	else if( SizeZ > 0 )
	{
		return static_cast<SIZE_T>(SizeX / GPixelFormats[Format].BlockSizeX) * (SizeY / GPixelFormats[Format].BlockSizeY) * (SizeZ / GPixelFormats[Format].BlockSizeZ) * GPixelFormats[Format].BlockBytes;
	}
	else
	{
		return static_cast<SIZE_T>(SizeX / GPixelFormats[Format].BlockSizeX) * (SizeY / GPixelFormats[Format].BlockSizeY) * GPixelFormats[Format].BlockBytes;
	}
}
