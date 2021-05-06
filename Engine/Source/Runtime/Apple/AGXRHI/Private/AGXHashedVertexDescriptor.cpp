// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXHashedVertexDescriptor.cpp: AGX RHI Hashed Vertex Descriptor.
=============================================================================*/


#include "AGXRHIPrivate.h"
#include "AGXHashedVertexDescriptor.h"


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Hashed Vertex Descriptor


FAGXHashedVertexDescriptor::FAGXHashedVertexDescriptor()
	: VertexDescHash(0)
	, VertexDesc(nil)
{
	// void
}

FAGXHashedVertexDescriptor::FAGXHashedVertexDescriptor(mtlpp::VertexDescriptor Desc, uint32 Hash)
	: VertexDescHash(Hash)
	, VertexDesc(Desc)
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
	// void
}

FAGXHashedVertexDescriptor& FAGXHashedVertexDescriptor::operator=(FAGXHashedVertexDescriptor const& Other)
{
	if (this != &Other)
	{
		VertexDescHash = Other.VertexDescHash;
		VertexDesc = Other.VertexDesc;
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
			if (VertexDesc.GetPtr() != Other.VertexDesc.GetPtr())
			{
				ns::Array<mtlpp::VertexBufferLayoutDescriptor> Layouts = VertexDesc.GetLayouts();
				ns::Array<mtlpp::VertexAttributeDescriptor> Attributes = VertexDesc.GetAttributes();

				ns::Array<mtlpp::VertexBufferLayoutDescriptor> OtherLayouts = Other.VertexDesc.GetLayouts();
				ns::Array<mtlpp::VertexAttributeDescriptor> OtherAttributes = Other.VertexDesc.GetAttributes();
				check(Layouts && Attributes && OtherLayouts && OtherAttributes);

				for (uint32 i = 0; bEqual && i < MaxVertexElementCount; i++)
				{
					mtlpp::VertexBufferLayoutDescriptor LayoutDesc = Layouts[(NSUInteger)i];
					mtlpp::VertexBufferLayoutDescriptor OtherLayoutDesc = OtherLayouts[(NSUInteger)i];

					bEqual &= ((LayoutDesc != nil) == (OtherLayoutDesc != nil));

					if (LayoutDesc && OtherLayoutDesc)
					{
						bEqual &= (LayoutDesc.GetStride() == OtherLayoutDesc.GetStride());
						bEqual &= (LayoutDesc.GetStepFunction() == OtherLayoutDesc.GetStepFunction());
						bEqual &= (LayoutDesc.GetStepRate() == OtherLayoutDesc.GetStepRate());
					}

					mtlpp::VertexAttributeDescriptor AttrDesc = Attributes[(NSUInteger)i];
					mtlpp::VertexAttributeDescriptor OtherAttrDesc = OtherAttributes[(NSUInteger)i];

					bEqual &= ((AttrDesc != nil) == (OtherAttrDesc != nil));

					if (AttrDesc && OtherAttrDesc)
					{
						bEqual &= (AttrDesc.GetFormat() == OtherAttrDesc.GetFormat());
						bEqual &= (AttrDesc.GetOffset() == OtherAttrDesc.GetOffset());
						bEqual &= (AttrDesc.GetBufferIndex() == OtherAttrDesc.GetBufferIndex());
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
