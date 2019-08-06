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
	/// This tests a dedicated server and a client that opens strait into the server.  
	/// </summary>
	public class BasicDedicatedServerTest : UnrealTestNode<ShooterTestConfig>
	{
		public BasicDedicatedServerTest(UnrealTestContext InContext) : base(InContext)
		{
		}

		public override ShooterTestConfig GetConfiguration()
		{
			ShooterTestConfig Config = base.GetConfiguration();
			Config.PreAssignAccount = false;
			Config.NoMCP = true;

			UnrealTestRole Client = Config.RequireRole(UnrealTargetRole.Client);
			UnrealTestRole Server = Config.RequireRole(UnrealTargetRole.Server);

			Client.Controllers.Add("BasicDedicatedServerTest");

			return Config;
		}
	}
}
