// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "InterchangeSourceData.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

namespace UE
{
	namespace Interchange
	{
		class FTextureTranslatorUtilities
		{
		public:
			static bool Generic2DTextureTranslate(const UInterchangeSourceData* SourceData, UInterchangeBaseNodeContainer& BaseNodeContainer);
		};
	}//ns Interchange
}//ns UE

