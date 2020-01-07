// Copyright Epic Games, Inc. All Rights Reserved.

using System;

using nDisplayLauncher.Log;


namespace nDisplayLauncher.Cluster.Config.Entity
{
	public class EntityInfo : EntityBase
	{
		public ConfigurationVersion Version { get; set; } = ConfigurationVersion.Ver22;

		public EntityInfo()
		{
		}

		public EntityInfo(string text)
		{
			try
			{
				InitializeFromText(text);
			}
			catch (Exception ex)
			{
				AppLogger.Log(ex.Message);
			}
		}

		public override void InitializeFromText(string text)
		{
			string StrVersion = Parser.GetStringValue(text, "version");
			Version = ConfigurationVersionHelpers.FromString(StrVersion);
		}
	}
}
