// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/ModelReport.h"
#include "MuR/SerialisationPrivate.h"


namespace mu
{

	//!
	struct REPORT_STATE
	{
		struct IMAGE
		{
			string m_name;
			string m_fragmentCode;
			vector<string> m_sourceImages;
			vector<string> m_sourceVectors;
			vector<string> m_sourceScalars;
		};

		struct COMPONENT
		{
			string m_name;
			vector<IMAGE> m_images;
		};

		struct LOD
		{
			vector<COMPONENT> m_components;
		};

		vector<LOD> m_lods;
	};


	//!
	class ModelReport::Private : public Base
	{
	public:

		Private()
		{
		}

		vector<REPORT_STATE> m_states;

	};

}
