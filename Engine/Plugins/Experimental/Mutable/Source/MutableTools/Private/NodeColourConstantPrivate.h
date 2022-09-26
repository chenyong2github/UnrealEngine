// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NodeColourConstant.h"
#include "NodeColourPrivate.h"

#include "MutableMath.h"


namespace mu
{

	class NodeColourConstant::Private : public NodeColour::Private
	{
	public:

		MUTABLE_DEFINE_CONST_VISITABLE()

	public:

		static NODE_TYPE s_type;

		vec3<float> m_value;

		//!
		void Serialise( OutputArchive& arch ) const
		{
			arch << m_value;
		}

		//!
		void Unserialise( InputArchive& arch )
		{
			arch >> m_value;
		}

	};

}
