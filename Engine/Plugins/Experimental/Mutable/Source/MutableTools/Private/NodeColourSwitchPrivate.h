// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NodeColourSwitch.h"
#include "NodeColourPrivate.h"
#include "NodeScalar.h"
#include "AST.h"


namespace mu
{


    class NodeColourSwitch::Private : public NodeColour::Private
	{
	public:

		MUTABLE_DEFINE_CONST_VISITABLE()

	public:

		static NODE_TYPE s_type;

		NodeScalarPtr m_pParameter;
        vector<NodeColourPtr> m_options;

		//!
		void Serialise( OutputArchive& arch ) const
		{
            uint32_t ver = 0;
			arch << ver;

			arch << m_pParameter;
			arch << m_options;
		}

		//!
		void Unserialise( InputArchive& arch )
		{
            uint32_t ver;
			arch >> ver;
			check(ver==0);

			arch >> m_pParameter;
			arch >> m_options;
		}
	};


}

