// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NodeImageSwizzle.h"
#include "NodeImagePrivate.h"
#include "ImagePrivate.h"


namespace mu
{


	class NodeImageSwizzle::Private : public NodeImage::Private
	{
	public:

		MUTABLE_DEFINE_CONST_VISITABLE()

	public:

		static NODE_TYPE s_type;

		EImageFormat m_format;
		vector<NodeImagePtr> m_sources;
		vector<int> m_sourceChannels;

		//!
		void Serialise( OutputArchive& arch ) const
		{
            uint32_t ver = 0;
			arch << ver;

			arch << (uint32)m_format;
			arch << m_sources;
			arch << m_sourceChannels;
		}

		//!
		void Unserialise( InputArchive& arch )
		{
            uint32_t ver;
			arch >> ver;
			check(ver==0);

			uint32 format;
			arch >> format;
			m_format = (EImageFormat)format;

			arch >> m_sources;
			arch >> m_sourceChannels;
		}
	};


}
