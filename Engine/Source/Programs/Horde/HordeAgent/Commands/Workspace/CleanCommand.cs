// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Logging;
using EpicGames.Core;
using EpicGames.Perforce.Managed;

namespace HordeAgent.Commands.Workspace
{
	[Command("Workspace", "Clean", "Cleans all modified files from the workspace.")]
	class CleanCommand : WorkspaceCommand
	{
		[CommandLine("-Incremental")]
		[Description("Performs an incremental sync, without removing intermediates")]
		public bool bIncrementalSync = false;

		protected override Task ExecuteAsync(ManagedWorkspace Repo, ILogger Logger)
		{
			return Repo.CleanAsync(!bIncrementalSync, CancellationToken.None);
		}
	}
}
