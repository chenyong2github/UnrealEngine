// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using BuildAgent.Workspace.Common;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace BuildAgent.Workspace
{
	[ProgramMode("Revert", "Revert all files that are open in the current workspace. Does not replace them with valid revisions.")]
	class RevertMode : WorkspaceMode
	{
		[CommandLine("-Client=", Required = true)]
		[Description("Client to revert all files for")]
		string ClientName = null;

		protected override void Execute(Repository Repo)
		{
			Repo.Revert(ClientName);
		}
	}
}
