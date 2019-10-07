// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using BuildAgent.Workspace.Common;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace BuildAgent.Workspace
{
	[ProgramMode("Clear", "Empties the staging directory of any files, returning them to the cache")]
	class ClearMode : WorkspaceMode
	{
		protected override void Execute(Repository Repo)
		{
			Repo.Clear();
		}
	}
}
