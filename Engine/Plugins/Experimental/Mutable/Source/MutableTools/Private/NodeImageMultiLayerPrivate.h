// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NodeImagePrivate.h"
#include "NodeImageMultiLayer.h"
#include "NodeColour.h"
#include "AST.h"

#include "MemoryPrivate.h"


namespace mu
{

    class NodeImageMultiLayer::Private : public NodeImage::Private
	{
	public:

		MUTABLE_DEFINE_CONST_VISITABLE()

	public:

		static NODE_TYPE s_type;

		NodeImagePtr m_pBase;
		NodeImagePtr m_pMask;
        NodeImagePtr m_pBlended;
        NodeRangePtr m_pRange;
		EBlendType m_type;

		//!
		void Serialise( OutputArchive& arch ) const
		{
            uint32_t ver = 0;
			arch << ver;
            arch << uint32_t(m_type);

			arch << m_pBase;
			arch << m_pMask;
            arch << m_pBlended;
            arch << m_pRange;
		}

		//!
		void Unserialise( InputArchive& arch )
		{
            uint32_t ver;
			arch >> ver;
			check(ver==0);

            uint32_t t;
            arch >> t;
            m_type=(EBlendType)t;

            arch >> m_pBase;
			arch >> m_pMask;
            arch >> m_pBlended;
            arch >> m_pRange;

		}
	};

}
