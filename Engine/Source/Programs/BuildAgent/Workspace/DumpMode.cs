// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using BuildAgent.Workspace.Common;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace BuildAgent.Workspace
{
	[ProgramMode("Dump", "Dumps the contents of the repository to the log for analysis")]
	class DumpMode : WorkspaceMode
	{
		protected override void Execute(Repository Repo)
		{
			Repo.Dump();
		}
	}
}
