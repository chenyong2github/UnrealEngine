// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NodePrivate.h"
#include "NodeScalarCurve.h"
#include "NodeScalar.h"
#include "AST.h"

#include "MemoryPrivate.h"
#include "SerialisationPrivate.h"
#include "ParametersPrivate.h"


namespace mu
{
	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------
    class NodeScalarCurve::Private : public Node::Private
	{
	public:

		MUTABLE_DEFINE_CONST_VISITABLE()

	public:

		static NODE_TYPE s_type;

		NodeScalarPtr m_input_scalar;
		Curve m_curve;

		//!
		void Serialise(OutputArchive& arch) const
		{
            uint32_t ver = 1;
			arch << ver;

			arch << m_input_scalar;
			arch << m_curve;
		}

		//!
		void Unserialise(InputArchive& arch)
		{
            uint32_t ver;
			arch >> ver;
            check(ver == 1);

			arch >> m_input_scalar;
			arch >> m_curve;
		}
	};


}
