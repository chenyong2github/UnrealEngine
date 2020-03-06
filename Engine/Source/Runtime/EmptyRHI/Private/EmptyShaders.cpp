// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	EmptyShaders.cpp: Empty shader RHI implementation.
=============================================================================*/

#include "EmptyRHIPrivate.h"


/** Initialization constructor. */
template<typename BaseResourceType>
TEmptyBaseShader<BaseResourceType>::TEmptyBaseShader(TArrayView<const uint8> Code)
{

}


/** Destructor */
template<typename BaseResourceType>
TEmptyBaseShader<BaseResourceType>::~TEmptyBaseShader()
{

}


FVertexShaderRHIRef FEmptyDynamicRHI::RHICreateVertexShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	FEmptyVertexShader* Shader = new FEmptyVertexShader(Code);
	return Shader;
}

FPixelShaderRHIRef FEmptyDynamicRHI::RHICreatePixelShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
	FEmptyPixelShader* Shader = new FEmptyPixelShader(Code);
	return Shader;
}

FHullShaderRHIRef FEmptyDynamicRHI::RHICreateHullShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{ 
	FEmptyHullShader* Shader = new FEmptyHullShader(Code);
	return Shader;
}

FDomainShaderRHIRef FEmptyDynamicRHI::RHICreateDomainShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{ 
	FEmptyDomainShader* Shader = new FEmptyDomainShader(Code);
	return Shader;
}

FGeometryShaderRHIRef FEmptyDynamicRHI::RHICreateGeometryShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{ 
	FEmptyGeometryShader* Shader = new FEmptyGeometryShader(Code);
	return Shader;
}

FComputeShaderRHIRef FEmptyDynamicRHI::RHICreateComputeShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{ 
	FEmptyComputeShader* Shader = new FEmptyComputeShader(Code);
	return Shader;
}


FEmptyBoundShaderState::FEmptyBoundShaderState(
			FRHIVertexDeclaration* InVertexDeclarationRHI,
			FRHIVertexShader* InVertexShaderRHI,
			FRHIPixelShader* InPixelShaderRHI,
			FRHIHullShader* InHullShaderRHI,
	FRHIDomainShader* InDomainShaderRHI,
	FRHIGeometryShader* InGeometryShaderRHI)
	:	CacheLink(InVertexDeclarationRHI,InVertexShaderRHI,InPixelShaderRHI,InHullShaderRHI,InDomainShaderRHI,InGeometryShaderRHI,this)
{
	FEmptyVertexDeclaration* InVertexDeclaration = FEmptyDynamicRHI::ResourceCast(InVertexDeclarationRHI);
	FEmptyVertexShader* InVertexShader = FEmptyDynamicRHI::ResourceCast(InVertexShaderRHI);
	FEmptyPixelShader* InPixelShader = FEmptyDynamicRHI::ResourceCast(InPixelShaderRHI);
	FEmptyHullShader* InHullShader = FEmptyDynamicRHI::ResourceCast(InHullShaderRHI);
	FEmptyDomainShader* InDomainShader = FEmptyDynamicRHI::ResourceCast(InDomainShaderRHI);
	FEmptyGeometryShader* InGeometryShader = FEmptyDynamicRHI::ResourceCast(InGeometryShaderRHI);

	// cache everything
	VertexDeclaration = InVertexDeclaration;
	VertexShader = InVertexShader;
	PixelShader = InPixelShader;
	HullShader = InHullShader;
	DomainShader = InDomainShader;
	GeometryShader = InGeometryShader;
}

FEmptyBoundShaderState::~FEmptyBoundShaderState()
{

}

FBoundShaderStateRHIRef FEmptyDynamicRHI::RHICreateBoundShaderState(
	FRHIVertexDeclaration* VertexDeclarationRHI,
	FRHIVertexShader* VertexShaderRHI,
	FRHIHullShader* HullShaderRHI,
	FRHIDomainShader* DomainShaderRHI,
	FRHIPixelShader* PixelShaderRHI,
	FRHIGeometryShader* GeometryShaderRHI
	)
{
	check(IsInRenderingThread());
	// Check for an existing bound shader state which matches the parameters
	FCachedBoundShaderStateLink* CachedBoundShaderStateLink = GetCachedBoundShaderState(
		VertexDeclarationRHI,
		VertexShaderRHI,
		PixelShaderRHI,
		HullShaderRHI,
		DomainShaderRHI,
		GeometryShaderRHI
		);

	if(CachedBoundShaderStateLink)
	{
		// If we've already created a bound shader state with these parameters, reuse it.
		return CachedBoundShaderStateLink->BoundShaderState;
	}
	else
	{
		return new FEmptyBoundShaderState(VertexDeclarationRHI,VertexShaderRHI,PixelShaderRHI,HullShaderRHI,DomainShaderRHI,GeometryShaderRHI);
	}
}
