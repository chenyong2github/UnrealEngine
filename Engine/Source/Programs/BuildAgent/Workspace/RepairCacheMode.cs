// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using BuildAgent.Workspace.Common;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace BuildAgent.Workspace
{
	[ProgramMode("RepairCache", "Checks the integrity of the cache, and removes any invalid files")]
	class RepairCacheMode : WorkspaceMode
	{
		protected override void Execute(Repository Repo)
		{
			Repo.Repair();
		}
	}
}
