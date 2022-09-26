// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MutableTools/Public/NodeImageProject.h"

#include "MutableTools/Private/NodeImagePrivate.h"
#include "MutableTools/Public/NodeScalar.h"
#include "MutableTools/Public/NodeColour.h"
#include "MutableTools/Public/NodeMesh.h"
#include "MutableTools/Public/NodeProjector.h"
#include "MutableRuntime/Private/MutableMath.h"

namespace mu
{


	class NodeImageProject::Private : public NodeImage::Private
	{
	public:

		MUTABLE_DEFINE_CONST_VISITABLE()

	public:

		static NODE_TYPE s_type;

		NodeProjectorPtr m_pProjector;
		NodeMeshPtr m_pMesh;
		NodeScalarPtr m_pAngleFadeStart;
		NodeScalarPtr m_pAngleFadeEnd;
		NodeImagePtr m_pImage;
		NodeImagePtr m_pMask;
        uint8_t m_layout = 0;
		FUintVector2 m_imageSize;

		//!
		void Serialise( OutputArchive& arch ) const
		{
            uint32_t ver = 2;
			arch << ver;

			arch << m_pProjector;
			arch << m_pMesh;
			arch << m_pAngleFadeStart;
			arch << m_pAngleFadeEnd;
			arch << m_pImage;
            arch << m_pMask;
            arch << m_layout;
			arch << m_imageSize;
        }

		//!
		void Unserialise( InputArchive& arch )
		{
            uint32_t ver;
			arch >> ver;
            check(ver==2);

			arch >> m_pProjector;
			arch >> m_pMesh;
			arch >> m_pAngleFadeStart;
			arch >> m_pAngleFadeEnd;
			arch >> m_pImage;
			arch >> m_pMask;
            arch >> m_layout;
			arch >> m_imageSize;
		}
	};


}
