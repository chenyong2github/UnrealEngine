// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Perforce.Managed;
using Microsoft.Extensions.Logging;
using EpicGames.Core;
using EpicGames.Perforce;

namespace HordeAgent.Commands.Workspace
{
	[Command("Workspace", "Setup", "Creates or updates a client to use a given stream")]
	class SetupCommand : WorkspaceCommand
	{
		[CommandLine("-Client=", Required = true)]
		[Description("Name of the client to create")]
		string ClientName = null!;

		[CommandLine("-Stream=", Required = true)]
		[Description("Name of the stream to configure")]
		string StreamName = null!;

		protected override Task ExecuteAsync(ManagedWorkspace Repo, ILogger Logger)
		{
			PerforceClientConnection PerforceClient = new PerforceClientConnection(Perforce, ClientName);
			return Repo.SetupAsync(PerforceClient, StreamName, CancellationToken.None);
		}
	}
}
