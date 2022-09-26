// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Visitor.h"

#include "AST.h"
#include "NodeImage.h"
#include "NodeImageConstant.h"
#include "NodeImageDifference.h"
#include "NodeImageInterpolate.h"
#include "NodeImageInterpolate3.h"
#include "NodeImageSaturate.h"
#include "NodeImageLuminance.h"
#include "NodeImageLayer.h"
#include "NodeImageLayerColour.h"
#include "NodeImageMultiLayer.h"
#include "NodeImageTable.h"
#include "NodeImageSwizzle.h"
#include "NodeImageFormat.h"
#include "NodeImageSelectColour.h"
#include "NodeImageColourMap.h"
#include "NodeImageGradient.h"
#include "NodeImageBinarise.h"
#include "NodeImageResize.h"
#include "NodeImagePlainColour.h"
#include "NodeImageSwitch.h"
#include "NodeImageVariation.h"
#include "NodeImageProject.h"
#include "NodeImageMipmap.h"
#include "NodeImageConditional.h"
#include "NodeImageParameter.h"
#include "NodeImageInvert.h"
#include "NodeImageNormalComposite.h"
#include "NodeImageTransform.h"

#include "ErrorLogPrivate.h"

#include "Operations.h"
#include "ImagePrivate.h"


#define MUTABLE_MISSING_IMAGE_DESC 	FImageDesc( FImageSize( 16, 16 ), EImageFormat::IF_RGB_UBYTE, 1 )


namespace mu
{

    //---------------------------------------------------------------------------------------------
    //! Generate the description of an image without generating its code.
    //---------------------------------------------------------------------------------------------
    class ImageDescGenerator : public Base,
                               public BaseVisitor,

                               // Standard operations from models
                               public Visitor<NodeImageConstant::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageDifference::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageInterpolate::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageInterpolate3::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageSaturate::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageLuminance::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageLayer::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageLayerColour::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageTable::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageSwizzle::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageFormat::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageSelectColour::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageColourMap::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageGradient::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageBinarise::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageResize::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImagePlainColour::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageSwitch::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageVariation::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageProject::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageMipmap::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageConditional::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageParameter::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageMultiLayer::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageInvert::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageNormalComposite::Private, Ptr<ASTOp>, true>,
                               public Visitor<NodeImageTransform::Private, Ptr<ASTOp>, true>
    {
    public:

        void Generate( const Node::Private& );

        Ptr<ASTOp> Visit( const NodeImageConstant::Private& );
        Ptr<ASTOp> Visit( const NodeImageDifference::Private& );
        Ptr<ASTOp> Visit( const NodeImageInterpolate::Private& );
        Ptr<ASTOp> Visit( const NodeImageInterpolate3::Private& );
        Ptr<ASTOp> Visit( const NodeImageSaturate::Private& );
        Ptr<ASTOp> Visit( const NodeImageLuminance::Private& );
        Ptr<ASTOp> Visit( const NodeImageLayer::Private& );
        Ptr<ASTOp> Visit( const NodeImageLayerColour::Private& );
        Ptr<ASTOp> Visit( const NodeImageTable::Private& );
        Ptr<ASTOp> Visit( const NodeImageSwizzle::Private&);
        Ptr<ASTOp> Visit( const NodeImageFormat::Private&);
        Ptr<ASTOp> Visit( const NodeImageSelectColour::Private& );
        Ptr<ASTOp> Visit( const NodeImageColourMap::Private& );
        Ptr<ASTOp> Visit( const NodeImageGradient::Private& );
        Ptr<ASTOp> Visit( const NodeImageBinarise::Private& );
        Ptr<ASTOp> Visit( const NodeImageResize::Private& );
        Ptr<ASTOp> Visit( const NodeImagePlainColour::Private& );
        Ptr<ASTOp> Visit( const NodeImageSwitch::Private& );
        Ptr<ASTOp> Visit( const NodeImageVariation::Private& );
        Ptr<ASTOp> Visit( const NodeImageProject::Private& );
        Ptr<ASTOp> Visit( const NodeImageMipmap::Private& );
        Ptr<ASTOp> Visit( const NodeImageConditional::Private& );
        Ptr<ASTOp> Visit( const NodeImageParameter::Private& );
        Ptr<ASTOp> Visit( const NodeImageMultiLayer::Private& );
        Ptr<ASTOp> Visit( const NodeImageInvert::Private& );
        Ptr<ASTOp> Visit( const NodeImageNormalComposite::Private& );
        Ptr<ASTOp> Visit( const NodeImageTransform::Private& );

    public:

        typedef map<const Node::Private*,FImageDesc> VisitedMap;
        VisitedMap m_compiled;

        //! Result
        FImageDesc m_desc;

    };

}
