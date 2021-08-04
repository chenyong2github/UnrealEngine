// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;

namespace UnrealBuildTool
{
	abstract class ActionExecutor
	{
		public abstract string Name
		{
			get;
		}

		public abstract bool ExecuteActions(List<LinkedAction> ActionsToExecute);
	}

}
