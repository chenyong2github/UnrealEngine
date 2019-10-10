// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using BuildAgent.Workspace.Common;

namespace BuildAgent.Workspace
{
	[ProgramMode("Clean", "Cleans all modified files from the workspace.")]
	class CleanMode : WorkspaceMode
	{
		protected override void Execute(Repository Repo)
		{
			Repo.Clean();
		}
	}
}
