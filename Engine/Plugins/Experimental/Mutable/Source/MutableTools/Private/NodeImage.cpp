// Copyright Epic Games, Inc. All Rights Reserved.


#include "NodeImage.h"
#include "NodePrivate.h"

#include "NodeImageConstant.h"
#include "NodeImageDifference.h"
#include "NodeImageInterpolate.h"
#include "NodeImageInterpolate3.h"
#include "NodeImageSaturate.h"
#include "NodeImageLuminance.h"
#include "NodeImageTable.h"
#include "NodeImageSwizzle.h"
#include "NodeImageFormat.h"
#include "NodeImageSelectColour.h"
#include "NodeImageColourMap.h"
#include "NodeImageGradient.h"
#include "NodeImageBinarise.h"
#include "NodeImageLayer.h"
#include "NodeImageLayerColour.h"
#include "NodeImageMultiLayer.h"
#include "NodeImageResize.h"
#include "NodeImagePlainColour.h"
#include "NodeImageProject.h"
#include "NodeImageMipmap.h"
#include "NodeImageSwitch.h"
#include "NodeImageConditional.h"
#include "NodeImageParameter.h"
#include "NodeImageInvert.h"
#include "NodeImageVariation.h"
#include "NodeImageNormalComposite.h"
#include "NodeImageTransform.h"


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	static NODE_TYPE s_nodeImageType = 	NODE_TYPE( "NodeImage", Node::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	void NodeImage::Serialise( const NodeImage* p, OutputArchive& arch )
	{
        uint32_t ver = 0;
		arch << ver;

		arch << uint32_t(p->Type);
		p->SerialiseWrapper(arch);
    }

        
	//---------------------------------------------------------------------------------------------
	NodeImagePtr NodeImage::StaticUnserialise( InputArchive& arch )
	{
        uint32_t ver;
		arch >> ver;
		check( ver == 0 );

        uint32_t id;
		arch >> id;

		switch (id)
		{
		case 1 :  return NodeImageConstant::StaticUnserialise( arch ); break;
		case 2 :  return NodeImageDifference::StaticUnserialise( arch ); break;
		//case 3 :  return NodeImageIdentity::StaticUnserialise( arch ); break;
		case 4 :  return NodeImageInterpolate::StaticUnserialise( arch ); break;
		case 6 :  return NodeImageSaturate::StaticUnserialise( arch ); break;
		//case 8 :  return NodeSelectImage::StaticUnserialise( arch ); break;
		case 9 :  return NodeImageTable::StaticUnserialise( arch ); break;
		case 10 :  return NodeImageSwizzle::StaticUnserialise( arch ); break;
		case 11 :  return NodeImageSelectColour::StaticUnserialise( arch ); break;
		case 12 :  return NodeImageColourMap::StaticUnserialise( arch ); break;
		case 13 :  return NodeImageGradient::StaticUnserialise( arch ); break;
		//case 14 :  return NodeImageVolumeLayer::StaticUnserialise( arch ); break;
		case 17 :  return NodeImageBinarise::StaticUnserialise( arch ); break;
		case 18 :  return NodeImageLuminance::StaticUnserialise( arch ); break;
		case 19 :  return NodeImageLayer::StaticUnserialise( arch ); break;
		case 20 :  return NodeImageLayerColour::StaticUnserialise( arch ); break;
		case 21 :  return NodeImageResize::StaticUnserialise( arch ); break;
		case 22 :  return NodeImagePlainColour::StaticUnserialise( arch ); break;
		case 23 :  return NodeImageInterpolate3::StaticUnserialise( arch ); break;
        case 24 :  return NodeImageProject::StaticUnserialise( arch ); break;
        case 25 :  return NodeImageMipmap::StaticUnserialise( arch ); break;
        case 26 :  return NodeImageSwitch::StaticUnserialise( arch ); break;
		case 27 :  return NodeImageConditional::StaticUnserialise( arch ); break;
        case 28 :  return NodeImageFormat::StaticUnserialise( arch ); break;
        case 29 :  return NodeImageParameter::StaticUnserialise( arch ); break;
        case 30 :  return NodeImageMultiLayer::StaticUnserialise( arch ); break;
        case 31 :  return NodeImageInvert::StaticUnserialise( arch ); break;
        case 32 :  return NodeImageVariation::StaticUnserialise( arch ); break;
        case 33 :  return NodeImageNormalComposite::StaticUnserialise( arch ); break;
        case 34 :  return NodeImageTransform::StaticUnserialise( arch ); break;
        default : check(false);
		}

		return 0;
	}


	//---------------------------------------------------------------------------------------------
	const NODE_TYPE* NodeImage::GetType() const
	{
		return GetStaticType();
	}


	//---------------------------------------------------------------------------------------------
	const NODE_TYPE* NodeImage::GetStaticType()
	{
		return &s_nodeImageType;
	}


}


