// Copyright Epic Games, Inc. All Rights Reserved.


#include "ImageDescGenerator.h"

#include "NodeImagePrivate.h"
#include "NodeImageConstantPrivate.h"
#include "NodeImageDifferencePrivate.h"
#include "NodeImageInterpolatePrivate.h"
#include "NodeImageInterpolate3Private.h"
#include "NodeImageSaturatePrivate.h"
#include "NodeImageLuminancePrivate.h"
#include "NodeImageTablePrivate.h"
#include "NodeImageSwizzlePrivate.h"
#include "NodeImageFormatPrivate.h"
#include "NodeImageSelectColourPrivate.h"
#include "NodeImageColourMapPrivate.h"
#include "NodeImageGradientPrivate.h"
#include "NodeImageBinarisePrivate.h"
#include "NodeImageLayerPrivate.h"
#include "NodeImageLayerColourPrivate.h"
#include "NodeImageMultiLayerPrivate.h"
#include "NodeImageResizePrivate.h"
#include "NodeImagePlainColourPrivate.h"
#include "NodeImageSwitchPrivate.h"
#include "NodeImageVariationPrivate.h"
#include "NodeImageProjectPrivate.h"
#include "NodeImageMipmapPrivate.h"
#include "NodeImageConditionalPrivate.h"
#include "NodeImageParameterPrivate.h"
#include "NodeImageInvertPrivate.h"
#include "NodeImageNormalCompositePrivate.h"
#include "NodeImageTransformPrivate.h"

#include "TablePrivate.h"

#include "MeshPrivate.h"
#include "ImagePrivate.h"
#include "Platform.h"



namespace mu
{


    //---------------------------------------------------------------------------------------------
    void ImageDescGenerator::Generate( const Node::Private& node )
    {
        VisitedMap::const_iterator it = m_compiled.find(&node);
        if ( it != m_compiled.end() )
        {
            m_desc = it->second;
        }
        else
        {
            // The result will be stored in m_size
            node.Accept( *this );
            m_compiled[ &node ] = m_desc;
        }
    }


    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImagePlainColour::Private& node )
    {
        m_desc = MUTABLE_MISSING_IMAGE_DESC;
        m_desc.m_format = EImageFormat::IF_RGB_UBYTE;
        m_desc.m_size[0] = (int16_t)node.m_sizeX;
        m_desc.m_size[1] = (int16_t)node.m_sizeY;

        return 0;
    }


    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImageConstant::Private& node )
    {
        m_desc = MUTABLE_MISSING_IMAGE_DESC;

        Ptr<const Image> pImage;
        if (node.m_pProxy) pImage = node.m_pProxy->Get();

        if (pImage)
        {
            m_desc.m_size[0]= pImage->GetSizeX();
            m_desc.m_size[1] = pImage->GetSizeY();
            m_desc.m_format = pImage->GetFormat();
            m_desc.m_lods = (uint8_t)pImage->GetLODCount();
        }

        return 0;
    }


    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImageParameter::Private& )
    {
        // We cannot know anything about the image at code generation time
        // \todo. Provide a template?
        m_desc = FImageDesc();
        return 0;
    }


    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImageTable::Private& node )
    {
        m_desc = MUTABLE_MISSING_IMAGE_DESC;

        // Verify that the table column is the right type
        int colIndex = node.m_pTable->FindColumn( node.m_columnName.c_str() );
        if ( colIndex<0 )
        {
            check( false );
            return 0;
        }

        Ptr<const Image> pImage;

        std::size_t i = 0;
        while ( !pImage && i<node.m_pTable->GetPrivate()->m_rows.size() )
        {
            pImage = node.m_pTable->GetPrivate()->m_rows[i].m_values[ colIndex ].m_pProxyImage->Get();
            ++i;
        }

        if (pImage)
        {
            m_desc.m_size[0] = pImage->GetSizeX();
            m_desc.m_size[1] = pImage->GetSizeY();
            m_desc.m_format = pImage->GetFormat();
            m_desc.m_lods = (uint8_t)pImage->GetLODCount();
        }

        return 0;
    }


    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImageResize::Private& node )
    {
        m_desc = MUTABLE_MISSING_IMAGE_DESC;

        if (node.m_pBase)
        {
            m_desc = MUTABLE_MISSING_IMAGE_DESC;
            Generate( *node.m_pBase->GetBasePrivate() );

            if ( node.m_relative )
            {
                m_desc.m_size[0] = uint16_t( m_desc.m_size[0] * node.m_sizeX );
                m_desc.m_size[1] = uint16_t( m_desc.m_size[1] * node.m_sizeY );
            }
            else
            {
                m_desc.m_size[0] = uint16_t( node.m_sizeX );
                m_desc.m_size[1] = uint16_t( node.m_sizeY );
            }
        }

        return 0;
    }


    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImageSaturate::Private& node )
    {
        m_desc = MUTABLE_MISSING_IMAGE_DESC;

        if ( NodeImage* pSource = node.m_pSource.get() )
        {
            Generate( *pSource->GetBasePrivate() );
        }

        return 0;
    }


    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImageMipmap::Private& node )
    {
        m_desc = MUTABLE_MISSING_IMAGE_DESC;

        if ( NodeImage* pSource = node.m_pSource.get() )
        {
            Generate( *pSource->GetBasePrivate() );

            int mipLevel = std::max(
                        (int)ceilf( logf( (float)m_desc.m_size[0] )/logf(2.0f) ),
                            (int)ceilf( logf( (float)m_desc.m_size[1] )/logf(2.0f) )
                            );

            m_desc.m_lods = std::max( m_desc.m_lods, (uint8_t)mipLevel );
        }

        return 0;
    }


    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImageLuminance::Private& node )
    {
        m_desc = MUTABLE_MISSING_IMAGE_DESC;

        if ( NodeImage* pSource = node.m_pSource.get() )
        {
            Generate( *pSource->GetBasePrivate() );
        }

        m_desc.m_format = EImageFormat::IF_L_UBYTE;

        return 0;
    }


    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImageInterpolate::Private& node )
    {
        m_desc = MUTABLE_MISSING_IMAGE_DESC;

        bool found = false;
        for ( std::size_t t=0; !found && t<node.m_targets.size(); ++t )
        {
            if ( NodeImage* pB = node.m_targets[t].get() )
            {
                found = true;
                Generate( *pB->GetBasePrivate() );
            }
        }

        m_desc.m_format = EImageFormat::IF_RGB_UBYTE;

        return 0;
    }


    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImageInterpolate3::Private& node )
    {
        m_desc = MUTABLE_MISSING_IMAGE_DESC;

        if ( node.m_pTarget0 )
        {
            Generate( *node.m_pTarget0->GetBasePrivate() );
        }
        else if ( node.m_pTarget1 )
        {
            Generate( *node.m_pTarget1->GetBasePrivate() );
        }
        else if ( node.m_pTarget2 )
        {
            Generate( *node.m_pTarget2->GetBasePrivate() );
        }

        m_desc.m_format = EImageFormat::IF_RGB_UBYTE;

        return 0;
    }


	//---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit(const NodeImageSwizzle::Private& node)
	{
		m_desc = MUTABLE_MISSING_IMAGE_DESC;

		// The base image size has higher priority
		bool found = false;
		for (std::size_t t = 0; !found && t<node.m_sources.size(); ++t)
		{
			if (NodeImage* pB = node.m_sources[t].get())
			{
				found = true;
				Generate(*pB->GetBasePrivate());
			}
		}

		m_desc.m_format = node.m_format;

		return 0;
	}


	//---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit(const NodeImageFormat::Private& node)
	{
		m_desc = MUTABLE_MISSING_IMAGE_DESC;

		if (node.m_source)
		{
			Generate(*node.m_source->GetBasePrivate());
		}

        // Necessary since dxt1 reports 4 channels just in case
        if (m_desc.m_format!=node.m_format)
        {
            if (GetImageFormatData(m_desc.m_format).m_channels>3
                    &&
                    node.m_formatIfAlpha!= EImageFormat::IF_NONE
                    )
            {
                m_desc.m_format = node.m_formatIfAlpha;
            }
            else
            {
                m_desc.m_format = node.m_format;
            }
        }

		return 0;
	}


    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImageDifference::Private& node )
    {
        m_desc = MUTABLE_MISSING_IMAGE_DESC;

        // The first image size has higher priority
        if ( NodeImage* pA = node.m_pA.get() )
        {
            Generate( *pA->GetBasePrivate() );
        }
        else if ( NodeImage* pB = node.m_pB.get() )
        {
            Generate( *pB->GetBasePrivate() );
        }

        m_desc.m_format = EImageFormat::IF_L_UBYTE;

        return 0;
    }


    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImageBinarise::Private& node )
    {
        m_desc = MUTABLE_MISSING_IMAGE_DESC;

        if ( NodeImage* pA = node.m_pBase.get() )
        {
            Generate( *pA->GetBasePrivate() );
        }

        m_desc.m_format = EImageFormat::IF_L_UBYTE;

        return 0;
    }


    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImageSelectColour::Private& node )
    {
        m_desc = MUTABLE_MISSING_IMAGE_DESC;

        if ( NodeImage* pBase = node.m_pSource.get() )
        {
            Generate( *pBase->GetBasePrivate() );
        }

        m_desc.m_format = EImageFormat::IF_L_UBYTE;

        return 0;
    }


    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImageColourMap::Private& node )
    {
        m_desc = MUTABLE_MISSING_IMAGE_DESC;

        if ( NodeImage* pBase = node.m_pBase.get() )
        {
            Generate( *pBase->GetBasePrivate() );
        }

        return 0;
    }


    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImageGradient::Private& node )
    {
        m_desc.m_format = EImageFormat::IF_RGB_UBYTE;
        m_desc.m_size[0] = (uint16_t)node.m_size[0];
        m_desc.m_size[1] = (uint16_t)node.m_size[1];
        m_desc.m_lods = 1;
        return 0;
    }


    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImageLayer::Private& node )
    {
        m_desc = MUTABLE_MISSING_IMAGE_DESC;

        // The base image size has higher priority
        if ( NodeImage* pBase = node.m_pBase.get() )
        {
            Generate( *pBase->GetBasePrivate() );
        }
        else if ( NodeImage* pMask = node.m_pMask.get() )
        {
            Generate( *pMask->GetBasePrivate() );
        }

        return 0;
    }


    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImageLayerColour::Private& node )
    {
        m_desc = MUTABLE_MISSING_IMAGE_DESC;

        // The base image size has higher priority
        if ( NodeImage* pBase = node.m_pBase.get() )
        {
            Generate( *pBase->GetBasePrivate() );
        }
        else if ( NodeImage* pMask = node.m_pMask.get() )
        {
            Generate( *pMask->GetBasePrivate() );
        }

        m_desc.m_format = EImageFormat::IF_RGB_UBYTE;

        return 0;
    }

    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImageMultiLayer::Private& node )
    {
        m_desc = MUTABLE_MISSING_IMAGE_DESC;

        // The base image size has higher priority
        if ( NodeImage* pBase = node.m_pBase.get() )
        {
            Generate( *pBase->GetBasePrivate() );
        }
        else if ( NodeImage* pMask = node.m_pMask.get() )
        {
            Generate( *pMask->GetBasePrivate() );
        }

        return 0;
    }

    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImageNormalComposite::Private& node )
    {
        m_desc = MUTABLE_MISSING_IMAGE_DESC;

        // The base image size has higher priority
        if ( NodeImage* pBase = node.m_pBase.get() )
        {
            Generate( *pBase->GetBasePrivate() );
        }

        return 0;
    }

    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImageSwitch::Private& node )
    {
        m_desc = MUTABLE_MISSING_IMAGE_DESC;

        if ( node.m_options.size() > 0 && node.m_options[0] )
        {
            Generate( *node.m_options[0]->GetBasePrivate() );
        }

        return 0;
    }


    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImageVariation::Private& node )
    {
        m_desc = MUTABLE_MISSING_IMAGE_DESC;

        if ( node.m_defaultImage )
        {
            Generate( *node.m_defaultImage->GetBasePrivate() );
        }
        else if ( node.m_variations.size() > 0 && node.m_variations[0].m_image )
        {
            Generate( *node.m_variations[0].m_image->GetBasePrivate() );
        }

        return 0;
    }


    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImageConditional::Private& node )
    {
        m_desc = MUTABLE_MISSING_IMAGE_DESC;

        if ( node.m_true )
        {
            Generate( *node.m_true->GetBasePrivate() );
        }
        else if ( node.m_false )
        {
            Generate( *node.m_false->GetBasePrivate() );
        }

        return 0;
    }


    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImageProject::Private& node )
    {
        m_desc = MUTABLE_MISSING_IMAGE_DESC;

		EImageFormat format = EImageFormat::IF_RGB_UBYTE;

        // Format from the image to project
        if ( node.m_pImage )
        {
            Generate( *node.m_pImage->GetBasePrivate() );
            format = m_desc.m_format;
        }

		// Size override?
		if (node.m_imageSize.X > 0 && node.m_imageSize.Y > 0)
		{
			m_desc.m_size[0] = uint16_t(node.m_imageSize[0]);
			m_desc.m_size[1] = uint16_t(node.m_imageSize[1]);
		}

        // Size from the mask
        else if ( node.m_pMask )
        {
            Generate( *node.m_pMask->GetBasePrivate() );
        }

        else
        {
			// \TODO: Warning?
            m_desc.m_size[0] = 256;
            m_desc.m_size[1] = 256;
        }

        m_desc.m_format = format;

        return 0;
    }


	//---------------------------------------------------------------------------------------------
	Ptr<ASTOp> ImageDescGenerator::Visit(const NodeImageInvert::Private& node)
	{
		m_desc = MUTABLE_MISSING_IMAGE_DESC;

		if (NodeImage* pA = node.m_pBase.get())
		{
			Generate(*pA->GetBasePrivate());
		}

		return 0;
	}

    //---------------------------------------------------------------------------------------------
    Ptr<ASTOp> ImageDescGenerator::Visit( const NodeImageTransform::Private& node )
    {
        m_desc = MUTABLE_MISSING_IMAGE_DESC;

        if ( NodeImage* pBase = node.m_pBase.get() )
        {
            Generate( *pBase->GetBasePrivate() );
        }

        return 0;
    }
}

