// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Perforce.Managed;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Perforce;

namespace Horde.Agent.Commands.Workspace
{
	[Command("Workspace", "Revert", "Revert all files that are open in the current workspace. Does not replace them with valid revisions.")]
	class RevertCommand : WorkspaceCommand
	{
		[CommandLine("-Client=", Required = true)]
		[Description("Client to revert all files for")]
		string ClientName { get; set; } = null!;

		protected override async Task ExecuteAsync(IPerforceConnection perforce, ManagedWorkspace repo, ILogger logger)
		{
			using IPerforceConnection perforceClient = await perforce.WithClientAsync(ClientName);
			await repo.RevertAsync(perforceClient, CancellationToken.None);
		}
	}
}
