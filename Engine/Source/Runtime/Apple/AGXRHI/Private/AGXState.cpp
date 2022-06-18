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

static MTLSamplerMinMagFilter TranslateZFilterMode(ESamplerFilter Filter)
{	switch (Filter)
	{
		case SF_Point:				return MTLSamplerMinMagFilterNearest;
		case SF_AnisotropicPoint:	return MTLSamplerMinMagFilterNearest;
		case SF_AnisotropicLinear:	return MTLSamplerMinMagFilterLinear;
		default:					return MTLSamplerMinMagFilterLinear;
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

static MTLBlendOperation TranslateBlendOp(EBlendOperation BlendOp)
{
	switch(BlendOp)
	{
		case BO_Subtract:	return MTLBlendOperationSubtract;
		case BO_Min:		return MTLBlendOperationMin;
		case BO_Max:		return MTLBlendOperationMax;
		default:			return MTLBlendOperationAdd;
	};
}


static MTLBlendFactor TranslateBlendFactor(EBlendFactor BlendFactor)
{
	switch (BlendFactor)
	{
		case BF_One:					return MTLBlendFactorOne;
		case BF_SourceColor:			return MTLBlendFactorSourceColor;
		case BF_InverseSourceColor:		return MTLBlendFactorOneMinusSourceColor;
		case BF_SourceAlpha:			return MTLBlendFactorSourceAlpha;
		case BF_InverseSourceAlpha:		return MTLBlendFactorOneMinusSourceAlpha;
		case BF_DestAlpha:				return MTLBlendFactorDestinationAlpha;
		case BF_InverseDestAlpha:		return MTLBlendFactorOneMinusDestinationAlpha;
		case BF_DestColor:				return MTLBlendFactorDestinationColor;
		case BF_InverseDestColor:		return MTLBlendFactorOneMinusDestinationColor;
		case BF_Source1Color:			return MTLBlendFactorSource1Color;
		case BF_InverseSource1Color:	return MTLBlendFactorOneMinusSource1Color;
		case BF_Source1Alpha:			return MTLBlendFactorSource1Alpha;
		case BF_InverseSource1Alpha:	return MTLBlendFactorOneMinusSource1Alpha;
		default:						return MTLBlendFactorZero;
	};
}

static MTLColorWriteMask TranslateWriteMask(EColorWriteMask WriteMask)
{
	uint32 Result = 0;
	Result |= (WriteMask & CW_RED)   ? (uint32)(MTLColorWriteMaskRed)   : 0;
	Result |= (WriteMask & CW_GREEN) ? (uint32)(MTLColorWriteMaskGreen) : 0;
	Result |= (WriteMask & CW_BLUE)  ? (uint32)(MTLColorWriteMaskBlue)  : 0;
	Result |= (WriteMask & CW_ALPHA) ? (uint32)(MTLColorWriteMaskAlpha) : 0;
	
	return (MTLColorWriteMask)Result;
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

template <typename InitializerType>
class FAGXStateObjectCache
{
public:
	FAGXStateObjectCache()
	{
	}
	
	~FAGXStateObjectCache()
	{
	}
	
	id Find(InitializerType Init)
	{
		if (IsRunningRHIInSeparateThread())
		{
			Mutex.ReadLock();
		}
		
		const TRefCountPtr<FNSObject>* State = Cache.Find(Init);

		if (IsRunningRHIInSeparateThread())
		{
			Mutex.ReadUnlock();
		}
		
		return State ? State->GetReference()->Get() : nil;
	}
	
	void Add(InitializerType Init, id State)
	{
		if (IsRunningRHIInSeparateThread())
		{
			Mutex.WriteLock();
		}
		
		Cache.Add(Init, new FNSObject(State, /* bRetain = */ true));
		
		if (IsRunningRHIInSeparateThread())
		{
			Mutex.WriteUnlock();
		}
	}
	
private:
	TMap<InitializerType, TRefCountPtr<FNSObject>> Cache;
	FRWLock Mutex;
};

static FAGXStateObjectCache<FSamplerStateInitializerRHI> Samplers;

static id<MTLSamplerState> FindOrCreateSamplerState(const FSamplerStateInitializerRHI& Initializer)
{
	id<MTLSamplerState> State = (id<MTLSamplerState>)Samplers.Find(Initializer);
	if (!State)
	{
		MTLSamplerDescriptor* Desc = [[MTLSamplerDescriptor alloc] init];
		
		switch (Initializer.Filter)
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
		State = [GMtlDevice newSamplerStateWithDescriptor:Desc];
		
		[Desc release];
		
		Samplers.Add(Initializer, State);

		[State release];
	}
	return State;
}

FAGXSamplerState::FAGXSamplerState(FSamplerStateInitializerRHI const& Initializer)
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

FAGXRasterizerState::FAGXRasterizerState(FRasterizerStateInitializerRHI const& Initializer)
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

static FAGXStateObjectCache<FDepthStencilStateInitializerRHI> DepthStencilStates;

FAGXDepthStencilState::FAGXDepthStencilState(FDepthStencilStateInitializerRHI const& InInitializer)
{
	Initializer = InInitializer;

	State = (id<MTLDepthStencilState>)DepthStencilStates.Find(Initializer);
	if (!State)
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
		State = [GMtlDevice newDepthStencilStateWithDescriptor:Desc];
		
		[Desc release];
		
		DepthStencilStates.Add(Initializer, State);

		[State release];
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
static FAGXStateObjectCache<FBlendStateInitializerRHI::FRenderTarget> BlendStates;
TMap<uint32, uint8> FAGXBlendState::BlendSettingsToUniqueKeyMap;
uint8 FAGXBlendState::NextKey = 0;
FCriticalSection FAGXBlendState::Mutex;

FAGXBlendState::FAGXBlendState(FBlendStateInitializerRHI const& Initializer)
{
	bUseIndependentRenderTargetBlendStates = Initializer.bUseIndependentRenderTargetBlendStates;
	bUseAlphaToCoverage = Initializer.bUseAlphaToCoverage;
	for (uint32 RenderTargetIndex = 0; RenderTargetIndex < MaxSimultaneousRenderTargets; ++RenderTargetIndex)
	{
		// which initializer to use
		FBlendStateInitializerRHI::FRenderTarget const& Init =
			Initializer.bUseIndependentRenderTargetBlendStates
				? Initializer.RenderTargets[RenderTargetIndex]
				: Initializer.RenderTargets[0];

		// make a new blend state
		MTLRenderPipelineColorAttachmentDescriptor* BlendState = (MTLRenderPipelineColorAttachmentDescriptor*)BlendStates.Find(Init);

		if (!BlendState)
		{
			BlendState = [[MTLRenderPipelineColorAttachmentDescriptor alloc] init];

			// set values
			[BlendState setBlendingEnabled:
				Init.ColorBlendOp != BO_Add || Init.ColorDestBlend != BF_Zero || Init.ColorSrcBlend != BF_One ||
				Init.AlphaBlendOp != BO_Add || Init.AlphaDestBlend != BF_Zero || Init.AlphaSrcBlend != BF_One];
			[BlendState setSourceRGBBlendFactor:TranslateBlendFactor(Init.ColorSrcBlend)];
			[BlendState setDestinationRGBBlendFactor:TranslateBlendFactor(Init.ColorDestBlend)];
			[BlendState setRgbBlendOperation:TranslateBlendOp(Init.ColorBlendOp)];
			[BlendState setSourceAlphaBlendFactor:TranslateBlendFactor(Init.AlphaSrcBlend)];
			[BlendState setDestinationAlphaBlendFactor:TranslateBlendFactor(Init.AlphaDestBlend)];
			[BlendState setAlphaBlendOperation:TranslateBlendOp(Init.AlphaBlendOp)];
			[BlendState setWriteMask:TranslateWriteMask(Init.ColorWriteMask)];
			
			BlendStates.Add(Init, BlendState);

			[BlendState release];
		}

		RenderTargetStates[RenderTargetIndex].BlendState = BlendState;

		// get the unique key
		uint32 BlendBitMask =
			((uint32)[BlendState sourceRGBBlendFactor] << 0) | ((uint32)[BlendState destinationRGBBlendFactor] << 4) | ((uint32)[BlendState rgbBlendOperation] << 8) |
			((uint32)[BlendState sourceAlphaBlendFactor] << 11) | ((uint32)[BlendState destinationAlphaBlendFactor] << 15) | ((uint32)[BlendState alphaBlendOperation] << 19) |
			((uint32)[BlendState writeMask] << 22);
		
		if (IsRunningRHIInSeparateThread())
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
		if (IsRunningRHIInSeparateThread())
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

