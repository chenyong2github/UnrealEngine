// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using nDisplayLauncher.Log;
using System;


namespace nDisplayLauncher.Cluster.Config.Entity
{
	public class EntityClusterNode : EntityBase
	{
		public string Id        { get; set; } = string.Empty;
		public string Window    { get; set; } = string.Empty;
		public bool   HasSound  { get; set; } = false;
		public bool   IsMaster  { get; set; } = false;
		public string Addr      { get; set; } = string.Empty;
		public int    PortCS    { get; set; } = 0;
		public int    PortSS    { get; set; } = 0;
		public int    PortCE    { get; set; } = 0;


		public EntityClusterNode()
		{
		}

		public EntityClusterNode(string text)
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
			Id       = Parser.GetStringValue(text, "id");
			Window   = Parser.GetStringValue(text, "window");
			HasSound = Parser.GetBoolValue(text, "sound", false);
			IsMaster = Parser.GetBoolValue(text, "master", false);
			Addr     = Parser.GetStringValue(text, "addr");
			PortCS   = Parser.GetIntValue(text, "port_cs", 41001);
			PortSS   = Parser.GetIntValue(text, "port_ss", 41002);
			PortCE   = Parser.GetIntValue(text, "port_ce", 41003);
		}
	}
}
