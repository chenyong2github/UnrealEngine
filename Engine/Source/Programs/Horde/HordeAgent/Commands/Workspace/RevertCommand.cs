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

namespace HordeAgent.Commands.Workspace
{
	[Command("Workspace", "Revert", "Revert all files that are open in the current workspace. Does not replace them with valid revisions.")]
	class RevertCommand : WorkspaceCommand
	{
		[CommandLine("-Client=", Required = true)]
		[Description("Client to revert all files for")]
		string ClientName = null!;

		protected override Task ExecuteAsync(ManagedWorkspace Repo, ILogger Logger)
		{
			PerforceClientConnection PerforceClient = new PerforceClientConnection(Perforce, ClientName);
			return Repo.RevertAsync(PerforceClient, CancellationToken.None);
		}
	}
}
