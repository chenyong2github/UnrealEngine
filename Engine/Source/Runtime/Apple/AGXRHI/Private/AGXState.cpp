// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXState.cpp: AGX RHI state implementation.
=============================================================================*/

#include "AGXRHIPrivate.h"
#include "AGXProfiler.h"

static uint32 GetMetalMaxAnisotropy(ESamplerFilter Filter, uint32 MaxAniso)
{
	switch (Filter)
	{
		case SF_AnisotropicPoint:
		case SF_AnisotropicLinear:	return ComputeAnisotropyRT(MaxAniso);
		default:					return 1;
	}
}

static mtlpp::SamplerMinMagFilter TranslateZFilterMode(ESamplerFilter Filter)
{	switch (Filter)
	{
		case SF_Point:				return mtlpp::SamplerMinMagFilter::Nearest;
		case SF_AnisotropicPoint:	return mtlpp::SamplerMinMagFilter::Nearest;
		case SF_AnisotropicLinear:	return mtlpp::SamplerMinMagFilter::Linear;
		default:					return mtlpp::SamplerMinMagFilter::Linear;
	}
}

static MTLSamplerAddressMode TranslateWrapMode(ESamplerAddressMode AddressMode)
{
	switch (AddressMode)
	{
		case AM_Clamp:	return MTLSamplerAddressModeClampToEdge;
		case AM_Mirror: return MTLSamplerAddressModeMirrorRepeat;
		case AM_Border: return MTLSamplerAddressModeClampToEdge;
		default:		return MTLSamplerAddressModeRepeat;
	}
}

static MTLCompareFunction TranslateCompareFunction(ECompareFunction CompareFunction)
{
	switch(CompareFunction)
	{
		case CF_Less:			return MTLCompareFunctionLess;
		case CF_LessEqual:		return MTLCompareFunctionLessEqual;
		case CF_Greater:		return MTLCompareFunctionGreater;
		case CF_GreaterEqual:	return MTLCompareFunctionGreaterEqual;
		case CF_Equal:			return MTLCompareFunctionEqual;
		case CF_NotEqual:		return MTLCompareFunctionNotEqual;
		case CF_Never:			return MTLCompareFunctionNever;
		default:				return MTLCompareFunctionAlways;
	};
}

static MTLCompareFunction TranslateSamplerCompareFunction(ESamplerCompareFunction SamplerComparisonFunction)
{
	switch(SamplerComparisonFunction)
	{
		case SCF_Less:		return MTLCompareFunctionLess;
		case SCF_Never:		return MTLCompareFunctionNever;
		default:			return MTLCompareFunctionNever;
	};
}

static MTLStencilOperation TranslateStencilOp(EStencilOp StencilOp)
{
	switch(StencilOp)
	{
		case SO_Zero:				return MTLStencilOperationZero;
		case SO_Replace:			return MTLStencilOperationReplace;
		case SO_SaturatedIncrement:	return MTLStencilOperationIncrementClamp;
		case SO_SaturatedDecrement:	return MTLStencilOperationDecrementClamp;
		case SO_Invert:				return MTLStencilOperationInvert;
		case SO_Increment:			return MTLStencilOperationIncrementWrap;
		case SO_Decrement:			return MTLStencilOperationDecrementWrap;
		default:					return MTLStencilOperationKeep;
	};
}

static mtlpp::BlendOperation TranslateBlendOp(EBlendOperation BlendOp)
{
	switch(BlendOp)
	{
		case BO_Subtract:	return mtlpp::BlendOperation::Subtract;
		case BO_Min:		return mtlpp::BlendOperation::Min;
		case BO_Max:		return mtlpp::BlendOperation::Max;
		default:			return mtlpp::BlendOperation::Add;
	};
}


static mtlpp::BlendFactor TranslateBlendFactor(EBlendFactor BlendFactor)
{
	switch(BlendFactor)
	{
		case BF_One:					return mtlpp::BlendFactor::One;
		case BF_SourceColor:			return mtlpp::BlendFactor::SourceColor;
		case BF_InverseSourceColor:		return mtlpp::BlendFactor::OneMinusSourceColor;
		case BF_SourceAlpha:			return mtlpp::BlendFactor::SourceAlpha;
		case BF_InverseSourceAlpha:		return mtlpp::BlendFactor::OneMinusSourceAlpha;
		case BF_DestAlpha:				return mtlpp::BlendFactor::DestinationAlpha;
		case BF_InverseDestAlpha:		return mtlpp::BlendFactor::OneMinusDestinationAlpha;
		case BF_DestColor:				return mtlpp::BlendFactor::DestinationColor;
		case BF_InverseDestColor:		return mtlpp::BlendFactor::OneMinusDestinationColor;
		case BF_Source1Color:			return mtlpp::BlendFactor::Source1Color;
		case BF_InverseSource1Color:	return mtlpp::BlendFactor::OneMinusSource1Color;
		case BF_Source1Alpha:			return mtlpp::BlendFactor::Source1Alpha;
		case BF_InverseSource1Alpha:	return mtlpp::BlendFactor::OneMinusSource1Alpha;
		default:						return mtlpp::BlendFactor::Zero;
	};
}

static mtlpp::ColorWriteMask TranslateWriteMask(EColorWriteMask WriteMask)
{
	uint32 Result = 0;
	Result |= (WriteMask & CW_RED) ? (mtlpp::ColorWriteMask::Red) : 0;
	Result |= (WriteMask & CW_GREEN) ? (mtlpp::ColorWriteMask::Green) : 0;
	Result |= (WriteMask & CW_BLUE) ? (mtlpp::ColorWriteMask::Blue) : 0;
	Result |= (WriteMask & CW_ALPHA) ? (mtlpp::ColorWriteMask::Alpha) : 0;
	
	return (mtlpp::ColorWriteMask)Result;
}

static EBlendOperation TranslateBlendOp(MTLBlendOperation BlendOp)
{
	switch(BlendOp)
	{
		case MTLBlendOperationSubtract:		return BO_Subtract;
		case MTLBlendOperationMin:			return BO_Min;
		case MTLBlendOperationMax:			return BO_Max;
		case MTLBlendOperationAdd: default:	return BO_Add;
	};
}


static EBlendFactor TranslateBlendFactor(MTLBlendFactor BlendFactor)
{
	switch(BlendFactor)
	{
		case MTLBlendFactorOne:							return BF_One;
		case MTLBlendFactorSourceColor:					return BF_SourceColor;
		case MTLBlendFactorOneMinusSourceColor:			return BF_InverseSourceColor;
		case MTLBlendFactorSourceAlpha:					return BF_SourceAlpha;
		case MTLBlendFactorOneMinusSourceAlpha:			return BF_InverseSourceAlpha;
		case MTLBlendFactorDestinationAlpha:			return BF_DestAlpha;
		case MTLBlendFactorOneMinusDestinationAlpha:	return BF_InverseDestAlpha;
		case MTLBlendFactorDestinationColor:			return BF_DestColor;
		case MTLBlendFactorOneMinusDestinationColor:	return BF_InverseDestColor;
		case MTLBlendFactorSource1Color:				return BF_Source1Color;
		case MTLBlendFactorOneMinusSource1Color:		return BF_InverseSource1Color;
		case MTLBlendFactorSource1Alpha:				return BF_Source1Alpha;
		case MTLBlendFactorOneMinusSource1Alpha:		return BF_InverseSource1Alpha;
		case MTLBlendFactorZero: default:				return BF_Zero;
	};
}

static EColorWriteMask TranslateWriteMask(MTLColorWriteMask WriteMask)
{
	uint32 Result = 0;
	Result |= (WriteMask & MTLColorWriteMaskRed) ? (CW_RED) : 0;
	Result |= (WriteMask & MTLColorWriteMaskGreen) ? (CW_GREEN) : 0;
	Result |= (WriteMask & MTLColorWriteMaskBlue) ? (CW_BLUE) : 0;
	Result |= (WriteMask & MTLColorWriteMaskAlpha) ? (CW_ALPHA) : 0;
	
	return (EColorWriteMask)Result;
}

template <typename InitializerType, typename StateType>
class FAGXStateObjectCache
{
public:
	FAGXStateObjectCache()
	{
		
	}
	
	~FAGXStateObjectCache()
	{
		
	}
	
	StateType Find(InitializerType Init)
	{
		if(IsRunningRHIInSeparateThread())
		{
			Mutex.ReadLock();
		}
		
		StateType* State = Cache.Find(Init);
		
		if(IsRunningRHIInSeparateThread())
		{
			Mutex.ReadUnlock();
		}
		
		return State ? *State : StateType(nullptr);
	}
	
	void Add(InitializerType Init, StateType const& State)
	{
		if(IsRunningRHIInSeparateThread())
		{
			Mutex.WriteLock();
		}
		
		Cache.Add(Init, State);
		
		if(IsRunningRHIInSeparateThread())
		{
			Mutex.WriteUnlock();
		}
	}
	
private:
	TMap<InitializerType, StateType> Cache;
	FRWLock Mutex;
};

static FAGXStateObjectCache<FSamplerStateInitializerRHI, FAGXSampler> Samplers;

static FAGXSampler FindOrCreateSamplerState(const FSamplerStateInitializerRHI& Initializer)
{
	FAGXSampler State = Samplers.Find(Initializer);
	if (!State.GetPtr())
	{
		MTLSamplerDescriptor* Desc = [[MTLSamplerDescriptor alloc] init];
		
		switch(Initializer.Filter)
		{
			case SF_AnisotropicLinear:
			case SF_AnisotropicPoint:
				[Desc setMinFilter:MTLSamplerMinMagFilterLinear];
				[Desc setMagFilter:MTLSamplerMinMagFilterLinear];
				[Desc setMipFilter:MTLSamplerMipFilterLinear];
				break;
			case SF_Trilinear:
				[Desc setMinFilter:MTLSamplerMinMagFilterLinear];
				[Desc setMagFilter:MTLSamplerMinMagFilterLinear];
				[Desc setMipFilter:MTLSamplerMipFilterLinear];
				break;
			case SF_Bilinear:
				[Desc setMinFilter:MTLSamplerMinMagFilterLinear];
				[Desc setMagFilter:MTLSamplerMinMagFilterLinear];
				[Desc setMipFilter:MTLSamplerMipFilterNearest];
				break;
			case SF_Point:
				[Desc setMinFilter:MTLSamplerMinMagFilterNearest];
				[Desc setMagFilter:MTLSamplerMinMagFilterNearest];
				[Desc setMipFilter:MTLSamplerMipFilterNearest];
				break;
		}
		[Desc setMaxAnisotropy:GetMetalMaxAnisotropy(Initializer.Filter, Initializer.MaxAnisotropy)];
		[Desc setSAddressMode:TranslateWrapMode(Initializer.AddressU)];
		[Desc setTAddressMode:TranslateWrapMode(Initializer.AddressV)];
		[Desc setRAddressMode:TranslateWrapMode(Initializer.AddressW)];
		[Desc setLodMinClamp:Initializer.MinMipLevel];
		[Desc setLodMaxClamp:Initializer.MaxMipLevel];
#if PLATFORM_TVOS
		[Desc setCompareFunction:MTLCompareFunctionNever];
#elif PLATFORM_IOS
		[Desc setCompareFunction:[GMtlDevice supportsFeatureSet:MTLFeatureSet_iOS_GPUFamily3_v1] ? TranslateSamplerCompareFunction(Initializer.SamplerComparisonFunction) : MTLCompareFunctionNever];
#else
		[Desc setCompareFunction:TranslateSamplerCompareFunction(Initializer.SamplerComparisonFunction)];
#endif
#if PLATFORM_MAC
		[Desc setBorderColor:Initializer.BorderColor == 0 ? MTLSamplerBorderColorTransparentBlack : MTLSamplerBorderColorOpaqueWhite];
#endif
		State = FAGXSampler([GMtlDevice newSamplerStateWithDescriptor:Desc], ns::Ownership::Assign);
		
		[Desc release];
		
		Samplers.Add(Initializer, State);
	}
	return State;
}

FAGXSamplerState::FAGXSamplerState(const FSamplerStateInitializerRHI& Initializer)
{
	State = FindOrCreateSamplerState(Initializer);
#if !PLATFORM_MAC
	if (GetMetalMaxAnisotropy(Initializer.Filter, Initializer.MaxAnisotropy))
	{
		FSamplerStateInitializerRHI Init = Initializer;
		Init.MaxAnisotropy = 1;
		NoAnisoState = FindOrCreateSamplerState(Init);
	}
#endif
}

FAGXSamplerState::~FAGXSamplerState()
{
}

FAGXRasterizerState::FAGXRasterizerState(const FRasterizerStateInitializerRHI& Initializer)
{
	State = Initializer;
}

FAGXRasterizerState::~FAGXRasterizerState()
{
	
}

bool FAGXRasterizerState::GetInitializer(FRasterizerStateInitializerRHI& Init)
{
	Init = State;
	return true;
}

static FAGXStateObjectCache<FDepthStencilStateInitializerRHI, mtlpp::DepthStencilState> DepthStencilStates;

FAGXDepthStencilState::FAGXDepthStencilState(const FDepthStencilStateInitializerRHI& InInitializer)
{
	Initializer = InInitializer;

	State = DepthStencilStates.Find(Initializer);
	if (!State.GetPtr())
	{
		MTLDepthStencilDescriptor* Desc = [[MTLDepthStencilDescriptor alloc] init];
		
		[Desc setDepthCompareFunction:TranslateCompareFunction(Initializer.DepthTest)];
		[Desc setDepthWriteEnabled:Initializer.bEnableDepthWrite];
		
		if (Initializer.bEnableFrontFaceStencil)
		{
			// set up front face stencil operations
			MTLStencilDescriptor* Stencil = [[MTLStencilDescriptor alloc] init];
			[Stencil setStencilCompareFunction:TranslateCompareFunction(Initializer.FrontFaceStencilTest)];
			[Stencil setStencilFailureOperation:TranslateStencilOp(Initializer.FrontFaceStencilFailStencilOp)];
			[Stencil setDepthFailureOperation:TranslateStencilOp(Initializer.FrontFaceDepthFailStencilOp)];
			[Stencil setDepthStencilPassOperation:TranslateStencilOp(Initializer.FrontFacePassStencilOp)];
			[Stencil setReadMask:Initializer.StencilReadMask];
			[Stencil setWriteMask:Initializer.StencilWriteMask];
			[Desc setFrontFaceStencil:Stencil];
			[Stencil release];
		}
		
		if (Initializer.bEnableBackFaceStencil)
		{
			// set up back face stencil operations
			MTLStencilDescriptor* Stencil = [[MTLStencilDescriptor alloc] init];
			[Stencil setStencilCompareFunction:TranslateCompareFunction(Initializer.BackFaceStencilTest)];
			[Stencil setStencilFailureOperation:TranslateStencilOp(Initializer.BackFaceStencilFailStencilOp)];
			[Stencil setDepthFailureOperation:TranslateStencilOp(Initializer.BackFaceDepthFailStencilOp)];
			[Stencil setDepthStencilPassOperation:TranslateStencilOp(Initializer.BackFacePassStencilOp)];
			[Stencil setReadMask:Initializer.StencilReadMask];
			[Stencil setWriteMask:Initializer.StencilWriteMask];
			[Desc setBackFaceStencil:Stencil];
			[Stencil release];
		}
		else if(Initializer.bEnableFrontFaceStencil)
		{
			// set up back face stencil operations to front face in single-face mode
			MTLStencilDescriptor* Stencil = [[MTLStencilDescriptor alloc] init];
			[Stencil setStencilCompareFunction:TranslateCompareFunction(Initializer.FrontFaceStencilTest)];
			[Stencil setStencilFailureOperation:TranslateStencilOp(Initializer.FrontFaceStencilFailStencilOp)];
			[Stencil setDepthFailureOperation:TranslateStencilOp(Initializer.FrontFaceDepthFailStencilOp)];
			[Stencil setDepthStencilPassOperation:TranslateStencilOp(Initializer.FrontFacePassStencilOp)];
			[Stencil setReadMask:Initializer.StencilReadMask];
			[Stencil setWriteMask:Initializer.StencilWriteMask];
			[Desc setBackFaceStencil:Stencil];
			[Stencil release];
		}
		
		// bake out the descriptor
		State = mtlpp::DepthStencilState([GMtlDevice newDepthStencilStateWithDescriptor:Desc], nullptr, ns::Ownership::Assign);
		
		[Desc release];
		
		DepthStencilStates.Add(Initializer, State);
	}
	
	// cache some pipeline state info
	bIsDepthWriteEnabled = Initializer.bEnableDepthWrite;
	bIsStencilWriteEnabled = Initializer.bEnableFrontFaceStencil || Initializer.bEnableBackFaceStencil;
}

FAGXDepthStencilState::~FAGXDepthStencilState()
{
}

bool FAGXDepthStencilState::GetInitializer(FDepthStencilStateInitializerRHI& Init)
{
	Init = Initializer;
	return true;
}



// statics
static FAGXStateObjectCache<FBlendStateInitializerRHI::FRenderTarget, mtlpp::RenderPipelineColorAttachmentDescriptor> BlendStates;
TMap<uint32, uint8> FAGXBlendState::BlendSettingsToUniqueKeyMap;
uint8 FAGXBlendState::NextKey = 0;
FCriticalSection FAGXBlendState::Mutex;

FAGXBlendState::FAGXBlendState(const FBlendStateInitializerRHI& Initializer)
{
	bUseIndependentRenderTargetBlendStates = Initializer.bUseIndependentRenderTargetBlendStates;
	bUseAlphaToCoverage = Initializer.bUseAlphaToCoverage;
	for(uint32 RenderTargetIndex = 0;RenderTargetIndex < MaxSimultaneousRenderTargets; ++RenderTargetIndex)
	{
		// which initializer to use
		const FBlendStateInitializerRHI::FRenderTarget& Init =
			Initializer.bUseIndependentRenderTargetBlendStates
				? Initializer.RenderTargets[RenderTargetIndex]
				: Initializer.RenderTargets[0];

		// make a new blend state
		mtlpp::RenderPipelineColorAttachmentDescriptor BlendState = BlendStates.Find(Init);

		if (!BlendState.GetPtr())
		{
			BlendState = mtlpp::RenderPipelineColorAttachmentDescriptor();
			
			// set values
			BlendState.SetBlendingEnabled(
				Init.ColorBlendOp != BO_Add || Init.ColorDestBlend != BF_Zero || Init.ColorSrcBlend != BF_One ||
				Init.AlphaBlendOp != BO_Add || Init.AlphaDestBlend != BF_Zero || Init.AlphaSrcBlend != BF_One);
			BlendState.SetSourceRgbBlendFactor(TranslateBlendFactor(Init.ColorSrcBlend));
			BlendState.SetDestinationRgbBlendFactor(TranslateBlendFactor(Init.ColorDestBlend));
			BlendState.SetRgbBlendOperation(TranslateBlendOp(Init.ColorBlendOp));
			BlendState.SetSourceAlphaBlendFactor(TranslateBlendFactor(Init.AlphaSrcBlend));
			BlendState.SetDestinationAlphaBlendFactor(TranslateBlendFactor(Init.AlphaDestBlend));
			BlendState.SetAlphaBlendOperation(TranslateBlendOp(Init.AlphaBlendOp));
			BlendState.SetWriteMask(TranslateWriteMask(Init.ColorWriteMask));
			
			BlendStates.Add(Init, BlendState);
		}
		
		RenderTargetStates[RenderTargetIndex].BlendState = BlendState;

		// get the unique key
		uint32 BlendBitMask =
			((uint32)BlendState.GetSourceRgbBlendFactor() << 0) | ((uint32)BlendState.GetDestinationRgbBlendFactor() << 4) | ((uint32)BlendState.GetRgbBlendOperation() << 8) |
			((uint32)BlendState.GetSourceAlphaBlendFactor() << 11) | ((uint32)BlendState.GetDestinationAlphaBlendFactor() << 15) | ((uint32)BlendState.GetAlphaBlendOperation() << 19) |
			((uint32)BlendState.GetWriteMask() << 22);
		
		
		if(IsRunningRHIInSeparateThread())
		{
			Mutex.Lock();
		}
		uint8* Key = BlendSettingsToUniqueKeyMap.Find(BlendBitMask);
		if (Key == NULL)
		{
			Key = &BlendSettingsToUniqueKeyMap.Add(BlendBitMask, NextKey++);

			// only giving limited bits to the key, since we need to pack 8 of them into a key
			checkf(NextKey < (1 << NumBits_BlendState), TEXT("Too many unique blend states to fit into the PipelineStateHash [%d allowed]"), 1 << NumBits_BlendState);
		}
		// set the key
		RenderTargetStates[RenderTargetIndex].BlendStateKey = *Key;
		if(IsRunningRHIInSeparateThread())
		{
			Mutex.Unlock();
		}
	}
}

FAGXBlendState::~FAGXBlendState()
{
}

bool FAGXBlendState::GetInitializer(FBlendStateInitializerRHI& Initializer)
{
	Initializer.bUseIndependentRenderTargetBlendStates = bUseIndependentRenderTargetBlendStates;
	Initializer.bUseAlphaToCoverage = bUseAlphaToCoverage;
	for(uint32 RenderTargetIndex = 0;RenderTargetIndex < MaxSimultaneousRenderTargets; ++RenderTargetIndex)
	{
		// which initializer to use
		FBlendStateInitializerRHI::FRenderTarget& Init = Initializer.RenderTargets[RenderTargetIndex];
		MTLRenderPipelineColorAttachmentDescriptor* CurrentState = RenderTargetStates[RenderTargetIndex].BlendState;
		
		if (CurrentState)
		{
			Init.ColorSrcBlend = TranslateBlendFactor(CurrentState.sourceRGBBlendFactor);
			Init.ColorDestBlend = TranslateBlendFactor(CurrentState.destinationRGBBlendFactor);
			Init.ColorBlendOp = TranslateBlendOp(CurrentState.rgbBlendOperation);
			Init.AlphaSrcBlend = TranslateBlendFactor(CurrentState.sourceAlphaBlendFactor);
			Init.AlphaDestBlend = TranslateBlendFactor(CurrentState.destinationAlphaBlendFactor);
			Init.AlphaBlendOp = TranslateBlendOp(CurrentState.alphaBlendOperation);
			Init.ColorWriteMask = TranslateWriteMask(CurrentState.writeMask);
		}
		
		if (!bUseIndependentRenderTargetBlendStates)
		{
			break;
		}
	}
	
	return true;
}





FSamplerStateRHIRef FAGXDynamicRHI::RHICreateSamplerState(const FSamplerStateInitializerRHI& Initializer)
{
    @autoreleasepool {
	return new FAGXSamplerState(Initializer);
	}
}

FRasterizerStateRHIRef FAGXDynamicRHI::RHICreateRasterizerState(const FRasterizerStateInitializerRHI& Initializer)
{
	@autoreleasepool {
    return new FAGXRasterizerState(Initializer);
	}
}

FDepthStencilStateRHIRef FAGXDynamicRHI::RHICreateDepthStencilState(const FDepthStencilStateInitializerRHI& Initializer)
{
	@autoreleasepool {
	return new FAGXDepthStencilState(Initializer);
	}
}


FBlendStateRHIRef FAGXDynamicRHI::RHICreateBlendState(const FBlendStateInitializerRHI& Initializer)
{
	@autoreleasepool {
	return new FAGXBlendState(Initializer);
	}
}

