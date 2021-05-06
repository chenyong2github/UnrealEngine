// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXVertexDeclaration.h: AGX RHI Vertex Declaration.
=============================================================================*/

#pragma once


#include "AGXHashedVertexDescriptor.h"


//------------------------------------------------------------------------------

#pragma mark - AGX Vertex Declaration Class


/**
 * This represents a vertex declaration that hasn't been combined with a
 * specific shader to create a bound shader.
 */
class FAGXVertexDeclaration : public FRHIVertexDeclaration
{
public:
	/** Initialization constructor. */
	FAGXVertexDeclaration(const FVertexDeclarationElementList& InElements);
	~FAGXVertexDeclaration();

	/** Cached element info array (offset, stream index, etc) */
	FVertexDeclarationElementList Elements;

	/** This is the layout for the vertex elements */
	FAGXHashedVertexDescriptor Layout;

	/** Hash without considering strides which may be overriden */
	uint32 BaseHash;

	virtual bool GetInitializer(FVertexDeclarationElementList& Init) override final;

protected:
	void GenerateLayout(const FVertexDeclarationElementList& Elements);
};
