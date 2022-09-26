// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NodeImagePrivate.h"
#include "NodeImageSaturate.h"
#include "NodeScalar.h"
#include "AST.h"

#include "MemoryPrivate.h"


namespace mu
{


	class NodeImageSaturate::Private : public NodeImage::Private
	{
	public:

		MUTABLE_DEFINE_CONST_VISITABLE()

	public:

		static NODE_TYPE s_type;

		NodeImagePtr m_pSource;
		NodeScalarPtr m_pFactor;

		//!
		void Serialise( OutputArchive& arch ) const
		{
            uint32_t ver = 0;
			arch << ver;

			arch << m_pSource;
			arch << m_pFactor;
		}

		//!
		void Unserialise( InputArchive& arch )
		{
            uint32_t ver;
			arch >> ver;
			check(ver==0);

			arch >> m_pSource;
			arch >> m_pFactor;
		}
	};


}
