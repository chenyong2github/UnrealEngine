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
	/// Basic Boot Test
	/// </summary>
	public class BootTest : EpicGameTestNode
	{
		public BootTest(UnrealTestContext InContext) : base (InContext)
		{
		}

		public override EpicGameTestConfig GetConfiguration()
		{
			EpicGameTestConfig Config = base.GetConfiguration();
			UnrealTestRole Client = Config.RequireRole(UnrealTargetRole.Client);
			Client.Controllers.Add("BootTest");

			return Config;
		}
	}
}
