// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using BuildAgent.Workspace.Common;
using Tools.DotNETCommon;

namespace BuildAgent.Workspace
{
	[ProgramMode("Setup", "Creates or updates a client to use a given stream")]
	class SetupMode : WorkspaceMode
	{
		[CommandLine("-Client=", Required = true)]
		[Description("Name of the client to create")]
		string ClientName = null;

		[CommandLine("-Stream=", Required = true)]
		[Description("Name of the stream to configure")]
		string StreamName = null;

		protected override void Execute(Repository Repo)
		{
			Repo.Setup(ClientName, StreamName);
		}
	}
}
