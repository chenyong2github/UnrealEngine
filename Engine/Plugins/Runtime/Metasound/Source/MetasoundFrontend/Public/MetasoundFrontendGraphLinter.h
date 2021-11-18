// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Set.h"
#include "Misc/Guid.h"
#include "Templates/Function.h"

namespace Metasound
{
	namespace Frontend
	{
		// Forward declare.
		class IInputController;
		class IOutputController;
		class INodeController;

		class METASOUNDFRONTEND_API FGraphLinter
		{
		public:
			using FDepthFirstVisitFunction = TFunctionRef<TSet<FGuid> (const INodeController&)>;

			/** Returns true if connecting thing input and output controllers will cause
			 * a loop in the graph. Returns false otherwise. 
			 */
			static bool DoesConnectionCauseLoop(const IInputController& InInputController, const IOutputController& InOutputController);

			/** Visits nodes using depth first traversals. */
			static void DepthFirstTraversal(const INodeController& Node, FDepthFirstVisitFunction Visit);
		};
	}
}
