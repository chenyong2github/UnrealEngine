// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeImageParameter.h"
#include "MuT/NodeImagePrivate.h"


namespace mu
{

    class NodeImageParameter::Private : public NodeImage::Private
	{
	public:

		MUTABLE_DEFINE_CONST_VISITABLE()

	public:

		static NODE_TYPE s_type;

		string m_name;
		string m_uid;

		//!
		void Serialise( OutputArchive& arch ) const
		{
            uint32_t ver = 1;
			arch << ver;

			arch << m_name;
			arch << m_uid;
		}

		//!
		void Unserialise( InputArchive& arch )
		{
            uint32_t ver;
			arch >> ver;
            check(ver==1);

			arch >> m_name;
            arch >> m_uid;
		}
	};

}
