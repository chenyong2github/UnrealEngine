// Copyright 1998-2019 Epic Games, Inc.All Rights Reserved.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using EpicGame;
using Gauntlet;

namespace ShooterTest
{
	/// <summary>
	/// This tests a listen server and a client that enters the server using quick match.  
	/// </summary>
	public class ListenServerQuickMatchTest : UnrealTestNode<ShooterTestConfig>
	{
		public ListenServerQuickMatchTest(UnrealTestContext InContext) : base(InContext)
		{
		}

		public override ShooterTestConfig GetConfiguration()
		{
			ShooterTestConfig Config = base.GetConfiguration();
			Config.PreAssignAccount = false;
			Config.NoMCP = true;

			IEnumerable<UnrealTestRole> Clients = Config.RequireRoles(UnrealTargetRole.Client, 2);
			Clients.ElementAt(0).Controllers.Add("ListenServerHost");
			Clients.ElementAt(1).Controllers.Add("ListenServerQuickMatchClient");

			return Config;
		}
	}
}
