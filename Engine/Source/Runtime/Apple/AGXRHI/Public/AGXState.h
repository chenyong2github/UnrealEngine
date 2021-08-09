// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXState.h: AGX RHI state definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
THIRD_PARTY_INCLUDES_START
#include "mtlpp.hpp"
THIRD_PARTY_INCLUDES_END

class FAGXSampler : public mtlpp::SamplerState
{
public:
	FAGXSampler(ns::Ownership retain = ns::Ownership::Retain) : mtlpp::SamplerState(nullptr, nullptr, retain) { }
	FAGXSampler(ns::Protocol<id<MTLSamplerState>>::type handle, ns::Ownership retain = ns::Ownership::Retain)
	: mtlpp::SamplerState(handle, nullptr, retain) {}
	
	FAGXSampler(mtlpp::SamplerState&& rhs)
	: mtlpp::SamplerState((mtlpp::SamplerState&&)rhs)
	{
		
	}
	
	FAGXSampler(const FAGXSampler& rhs)
	: mtlpp::SamplerState(rhs)
	{
		
	}
	
	FAGXSampler(const SamplerState& rhs)
	: mtlpp::SamplerState(rhs)
	{
		
	}
	
	FAGXSampler(FAGXSampler&& rhs)
	: mtlpp::SamplerState((mtlpp::SamplerState&&)rhs)
	{
		
	}
	
	FAGXSampler& operator=(const FAGXSampler& rhs)
	{
		if (this != &rhs)
		{
			mtlpp::SamplerState::operator=(rhs);
		}
		return *this;
	}
	
	FAGXSampler& operator=(FAGXSampler&& rhs)
	{
		mtlpp::SamplerState::operator=((mtlpp::SamplerState&&)rhs);
		return *this;
	}
	
	inline bool operator==(FAGXSampler const& rhs) const
	{
		return GetPtr() == rhs.GetPtr();
	}
	
	inline bool operator!=(FAGXSampler const& rhs) const
	{
		return GetPtr() != rhs.GetPtr();
	}
	
	friend uint32 GetTypeHash(FAGXSampler const& Hash)
	{
		return GetTypeHash(Hash.GetPtr());
	}
};

class FAGXSamplerState : public FRHISamplerState
{
public:
	
	/** 
	 * Constructor/destructor
	 */
	FAGXSamplerState(const FSamplerStateInitializerRHI& Initializer);
	~FAGXSamplerState();

	FAGXSampler State;
#if !PLATFORM_MAC
	FAGXSampler NoAnisoState;
#endif
};

class FAGXRasterizerState : public FRHIRasterizerState
{
public:

	/**
	 * Constructor/destructor
	 */
	FAGXRasterizerState(const FRasterizerStateInitializerRHI& Initializer);
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
	FAGXDepthStencilState(const FDepthStencilStateInitializerRHI& Initializer);
	~FAGXDepthStencilState();
	
	virtual bool GetInitializer(FDepthStencilStateInitializerRHI& Init) override final;
	
	FDepthStencilStateInitializerRHI Initializer;
	mtlpp::DepthStencilState State;
	bool bIsDepthWriteEnabled;
	bool bIsStencilWriteEnabled;
};

class FAGXBlendState : public FRHIBlendState
{
public:

	/**
	 * Constructor/destructor
	 */
	FAGXBlendState(const FBlendStateInitializerRHI& Initializer);
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
