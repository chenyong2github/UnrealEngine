// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXHashedVertexDescriptor.h: AGX RHI Hashed Vertex Descriptor.
=============================================================================*/

#pragma once


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Hashed Vertex Descriptor


/**
 * The MTLVertexDescriptor and a pre-calculated hash value used to simplify
 * comparisons (as vendor MTLVertexDescriptor implementations are not all
 * comparable).
 */
struct FAGXHashedVertexDescriptor
{
	NSUInteger VertexDescHash;
	mtlpp::VertexDescriptor VertexDesc;

	FAGXHashedVertexDescriptor();
	FAGXHashedVertexDescriptor(mtlpp::VertexDescriptor Desc, uint32 Hash);
	FAGXHashedVertexDescriptor(FAGXHashedVertexDescriptor const& Other);
	~FAGXHashedVertexDescriptor();

	FAGXHashedVertexDescriptor& operator=(FAGXHashedVertexDescriptor const& Other);
	bool operator==(FAGXHashedVertexDescriptor const& Other) const;

	friend uint32 GetTypeHash(FAGXHashedVertexDescriptor const& Hash)
	{
		return Hash.VertexDescHash;
	}
};
