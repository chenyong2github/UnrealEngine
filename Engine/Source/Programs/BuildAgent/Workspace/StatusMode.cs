// Copyright Epic Games, Inc. All Rights Reserved.

using BuildAgent.Workspace.Common;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace BuildAgent.Workspace
{
	[ProgramMode("Status", "Prints information about the state of the cache and workspace")]
	class StatusMode : WorkspaceMode
	{
		protected override void Execute(Repository Repo)
		{
			Repo.Status();
		}
	}
}
