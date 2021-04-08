// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AddonTools.h"

BEGIN_NAMESPACE_UE_AC

class F3DElement2String
{
  public:
	static utf8_string Element2String(const ModelerAPI::Element& InModelElement);

	static utf8_string Body2String(const ModelerAPI::MeshBody& InBodyElement);
};

END_NAMESPACE_UE_AC
