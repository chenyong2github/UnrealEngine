// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXState.h: AGX RHI state definitions.
=============================================================================*/

#pragma once

class FAGXSamplerState : public FRHISamplerState
{
public:
	
	/** 
	 * Constructor/destructor
	 */
	FAGXSamplerState(const FSamplerStateInitializerRHI& Initializer);
	~FAGXSamplerState();

	id<MTLSamplerState> State;
#if !PLATFORM_MAC
	id<MTLSamplerState> NoAnisoState;
#endif
};

class FAGXRasterizerState : public FRHIRasterizerState
{
public:

	/**
	 * Constructor/destructor
	 */
	FAGXRasterizerState(FRasterizerStateInitializerRHI const& Initializer);
	~FAGXRasterizerState();
	
	virtual bool GetInitializer(FRasterizerStateInitializerRHI& Init) override final;
	
	FRasterizerStateInitializerRHI State;
};

class FAGXDepthStencilState : public FRHIDepthStencilState
{
public:

	/**
	 * Constructor/destructor
	 */
	FAGXDepthStencilState(FDepthStencilStateInitializerRHI const& Initializer);
	~FAGXDepthStencilState();
	
	virtual bool GetInitializer(FDepthStencilStateInitializerRHI& Init) override final;
	
	FDepthStencilStateInitializerRHI Initializer;
	id<MTLDepthStencilState> State;
	bool bIsDepthWriteEnabled;
	bool bIsStencilWriteEnabled;
};

class FAGXBlendState : public FRHIBlendState
{
public:

	/**
	 * Constructor/destructor
	 */
	FAGXBlendState(FBlendStateInitializerRHI const& Initializer);
	~FAGXBlendState();
	
	virtual bool GetInitializer(FBlendStateInitializerRHI& Init) override final;

	struct FBlendPerMRT
	{
		mtlpp::RenderPipelineColorAttachmentDescriptor BlendState;
		uint8 BlendStateKey;
	};
	FBlendPerMRT RenderTargetStates[MaxSimultaneousRenderTargets];
	bool bUseIndependentRenderTargetBlendStates;
	bool bUseAlphaToCoverage;

private:
	// this tracks blend settings (in a bit flag) into a unique key that uses few bits, for PipelineState MRT setup
	static TMap<uint32, uint8> BlendSettingsToUniqueKeyMap;
	static uint8 NextKey;
	static FCriticalSection Mutex;
};
