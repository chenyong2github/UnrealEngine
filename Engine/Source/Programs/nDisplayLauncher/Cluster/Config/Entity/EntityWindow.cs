// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;

using nDisplayLauncher.Log;


namespace nDisplayLauncher.Cluster.Config.Entity
{
	public class EntityWindow : EntityBase
	{
		public string Id           { get; set; } = string.Empty;
		public bool   IsFullscreen { get; set; } = false;
		public int    WinX         { get; set; } = -1;
		public int    WinY         { get; set; } = -1;
		public int    ResX         { get; set; } = -1;
		public int    ResY         { get; set; } = -1;
		public List<string> Viewports { get; set; } = new List<string>();


		public EntityWindow()
		{
		}

		public EntityWindow(string text)
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
			Id = Parser.GetStringValue(text, "id");
			IsFullscreen = Parser.GetBoolValue(text, "fullscreen", false);
			WinX = Parser.GetIntValue(text, "winx", -1);
			WinY = Parser.GetIntValue(text, "winy", -1);
			ResX = Parser.GetIntValue(text, "resx", -1);
			ResY = Parser.GetIntValue(text, "resy", -1);
			Viewports = Parser.GetStringArrayValue(text, "viewports");
		}
	}
}
