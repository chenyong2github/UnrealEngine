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
	public class ShooterTestConfig : EpicGameTestConfig
	{
		[AutoParam]
		public bool NoSteam = false;

		[AutoParam]
		public bool WithPacketHandlerEncryption = false;

		[AutoParam]
		public bool NoSeamlessTravel = false;

		[AutoParam]
		public int TargetNumOfCycledMatches = 2;

		public override void ApplyToConfig(UnrealAppConfig AppConfig, UnrealSessionRole ConfigRole, IEnumerable<UnrealSessionRole> OtherRoles)
		{
			base.ApplyToConfig(AppConfig, ConfigRole, OtherRoles);

			if (NoSteam)
			{
				AppConfig.CommandLine += " -nosteam";
			}

			if (WithPacketHandlerEncryption)
			{
				AppConfig.CommandLine += "-ini:Engine:[PacketHandlerComponents]:EncryptionComponent=AESHandlerComponent -execcmds=\"ShooterGame.TestEncryption 1\"";
			}

			if (NoSeamlessTravel)
			{
				AppConfig.CommandLine += " -noseamlesstravel";
			}

			if (AppConfig.ProcessType.IsClient())
			{
				AppConfig.CommandLine += string.Format(" -TargetNumOfCycledMatches={0}", TargetNumOfCycledMatches);
			}

			const float InitTime = 120.0f;
			const float MatchTime = 300.0f;
			MaxDuration = InitTime + (MatchTime * TargetNumOfCycledMatches);
		}
	}
}
