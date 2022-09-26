// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NodeImageInterpolate.h"
#include "NodeImagePrivate.h"
#include "NodeScalar.h"


namespace mu
{


	class NodeImageInterpolate::Private : public NodeImage::Private
	{
	public:

		MUTABLE_DEFINE_CONST_VISITABLE()

	public:

		static NODE_TYPE s_type;

		NodeScalarPtr m_pFactor;
		vector<NodeImagePtr> m_targets;

		//!
		void Serialise( OutputArchive& arch ) const
		{
            uint32_t ver = 0;
			arch << ver;

			arch << m_pFactor;
			arch << m_targets;
		}

		//!
		void Unserialise( InputArchive& arch )
		{
            uint32_t ver;
			arch >> ver;
			check(ver==0);

			arch >> m_pFactor;
			arch >> m_targets;
		}
	};


}
