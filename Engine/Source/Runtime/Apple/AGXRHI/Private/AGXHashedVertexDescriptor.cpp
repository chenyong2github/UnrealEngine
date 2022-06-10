// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXHashedVertexDescriptor.cpp: AGX RHI Hashed Vertex Descriptor.
=============================================================================*/


#include "AGXRHIPrivate.h"
#include "AGXHashedVertexDescriptor.h"


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Hashed Vertex Descriptor


FAGXHashedVertexDescriptor::FAGXHashedVertexDescriptor(MTLVertexDescriptor* Desc, uint32 Hash)
	: VertexDescHash(Hash)
	, VertexDesc([Desc retain])
{
	// void
}

FAGXHashedVertexDescriptor::FAGXHashedVertexDescriptor(FAGXHashedVertexDescriptor const& Other)
	: VertexDescHash(0)
	, VertexDesc(nil)
{
	operator=(Other);
}

FAGXHashedVertexDescriptor::~FAGXHashedVertexDescriptor()
{
	[VertexDesc release];
}

FAGXHashedVertexDescriptor& FAGXHashedVertexDescriptor::operator=(FAGXHashedVertexDescriptor const& Other)
{
	if (this != &Other)
	{
		VertexDescHash = Other.VertexDescHash;
		[VertexDesc release];
		VertexDesc = [Other.VertexDesc retain];
	}
	return *this;
}

bool FAGXHashedVertexDescriptor::operator==(FAGXHashedVertexDescriptor const& Other) const
{
	bool bEqual = false;

	if (this != &Other)
	{
		if (VertexDescHash == Other.VertexDescHash)
		{
			bEqual = true;
			if (VertexDesc != Other.VertexDesc)
			{
				for (uint32 Index = 0; bEqual && (Index < MaxVertexElementCount); ++Index)
				{
					bEqual &= ((VertexDesc.layouts[Index] != nil) == (Other.VertexDesc.layouts[Index] != nil));

					if (bEqual && (VertexDesc.layouts[Index] != nil))
					{
						bEqual &= (VertexDesc.layouts[Index].stride         == Other.VertexDesc.layouts[Index].stride);
						bEqual &= (VertexDesc.layouts[Index].stepFunction   == Other.VertexDesc.layouts[Index].stepFunction);
						bEqual &= (VertexDesc.layouts[Index].stepRate       == Other.VertexDesc.layouts[Index].stepRate);
					}

					bEqual &= ((VertexDesc.attributes[Index] != nil) == (Other.VertexDesc.attributes[Index] != nil));

					if (bEqual && (VertexDesc.attributes[Index] != nil))
					{
						bEqual &= (VertexDesc.attributes[Index].format      == Other.VertexDesc.attributes[Index].format);
						bEqual &= (VertexDesc.attributes[Index].offset      == Other.VertexDesc.attributes[Index].offset);
						bEqual &= (VertexDesc.attributes[Index].bufferIndex == Other.VertexDesc.attributes[Index].bufferIndex);
					}
				}
			}
		}
	}
	else
	{
		bEqual = true;
	}

	return bEqual;
}
