// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NodeScalarPrivate.h"

#include "NodeScalarParameter.h"
#include "NodeImage.h"
#include "NodeRange.h"
#include "MemoryPrivate.h"
#include "ParametersPrivate.h"


namespace mu
{


	class NodeScalarParameter::Private : public NodeScalar::Private
	{
	public:

		MUTABLE_DEFINE_CONST_VISITABLE()

	public:

		static NODE_TYPE s_type;

		float m_defaultValue = 0.0f;
		string m_name;
		string m_uid;
		PARAMETER_DETAILED_TYPE m_detailedType = PARAMETER_DETAILED_TYPE::UNKNOWN;

        vector<Ptr<NodeImage>> m_additionalImages;

        vector<Ptr<NodeRange>> m_ranges;

		//!
		void Serialise( OutputArchive& arch ) const
		{
            uint32_t ver = 4;
			arch << ver;

			arch << m_defaultValue;
			arch << m_name;
			arch << m_uid;
			arch << m_additionalImages;
			arch << m_detailedType;
            arch << m_ranges;
        }

		//!
		void Unserialise( InputArchive& arch )
		{
            uint32_t ver;
			arch >> ver;
            check(ver==4);

			arch >> m_defaultValue;
			arch >> m_name;
            arch >> m_uid;
			arch >> m_additionalImages;
            arch >> m_detailedType;
            arch >> m_ranges;
        }
	};

}
