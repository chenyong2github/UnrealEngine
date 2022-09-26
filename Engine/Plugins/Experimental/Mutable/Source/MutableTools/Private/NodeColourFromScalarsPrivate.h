// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NodeColourPrivate.h"
#include "NodeColourFromScalars.h"
#include "NodeScalar.h"
#include "NodeImage.h"
#include "AST.h"

#include "MemoryPrivate.h"


namespace mu
{


	class NodeColourFromScalars::Private : public NodeColour::Private
	{
	public:

		MUTABLE_DEFINE_CONST_VISITABLE()

	public:

		static NODE_TYPE s_type;

		NodeScalarPtr m_pX;
		NodeScalarPtr m_pY;
		NodeScalarPtr m_pZ;
		NodeScalarPtr m_pW;

		//!
		void Serialise( OutputArchive& arch ) const
		{
            uint32_t ver = 0;
			arch << ver;

			arch << m_pX;
			arch << m_pY;
			arch << m_pZ;
			arch << m_pW;
		}

		//!
		void Unserialise( InputArchive& arch )
		{
            uint32_t ver;
			arch >> ver;
			check(ver==0);

			arch >> m_pX;
			arch >> m_pY;
			arch >> m_pZ;
			arch >> m_pW;
		}
	};


}
