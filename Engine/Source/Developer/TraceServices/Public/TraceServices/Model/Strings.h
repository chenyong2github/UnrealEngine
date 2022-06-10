// Copyright Epic Games, Inc. All Rights Reserved.
#pragma  once

#include "TraceServices/Model/AnalysisSession.h"
#include "Templates/Function.h"

namespace TraceServices
{
	struct FStringDefinition
	{
		/**
		 * Display string. The pointer is valid during the analysis session.
		 */
		const TCHAR* Display;
	};
}