// Copyright Epic Games, Inc. All Rights Reserved.

using BuildAgent.Workspace.Common;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace BuildAgent.Workspace
{
	[ProgramMode("PurgeCache", "Shrink the size of the cache to the given size")]
	class PurgeCacheMode : WorkspaceMode
	{
		[CommandLine("-Size=")]
		string SizeParam = null;

		protected override void Execute(Repository Repo)
		{
			long Size = 0;
			if (SizeParam != null)
			{
				Size = ParseSize(SizeParam);
			}

			Repo.Purge(Size);
		}
	}
}
