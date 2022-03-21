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
using EpicGames.Perforce;

namespace Horde.Agent.Commands.Workspace
{
	[Command("Workspace", "Clean", "Cleans all modified files from the workspace.")]
	class CleanCommand : WorkspaceCommand
	{
		[CommandLine("-Incremental")]
		[Description("Performs an incremental sync, without removing intermediates")]
		public bool Incremental { get; set; } = false;

		protected override Task ExecuteAsync(IPerforceConnection perforce, ManagedWorkspace repo, ILogger logger)
		{
			return repo.CleanAsync(!Incremental, CancellationToken.None);
		}
	}
}
