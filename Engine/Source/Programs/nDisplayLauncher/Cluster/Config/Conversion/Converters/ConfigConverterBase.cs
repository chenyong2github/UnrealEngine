// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace nDisplayLauncher.Cluster.Config.Conversion.Converters
{
	abstract class ConfigConverterBase
	{
		public enum ETokenType
		{
			Other,
			Info,
			ClusterNode,
			Window,
			Screen,
			Viewport,
			Projection,
			Camera,
			SceneNode,
			Input,
			Stereo,
			Network,
			Debug,
			General,
			Custom
		}

		public string GetTokenShortName(ETokenType type)
		{
			switch (type)
			{
				case ETokenType.Other: return        string.Empty;
				case ETokenType.Info: return         "info";
				case ETokenType.ClusterNode: return  "cluster_node";
				case ETokenType.Window: return       "window";
				case ETokenType.Screen: return       "screen";
				case ETokenType.Viewport: return     "viewport";
				case ETokenType.Projection: return   "projection";
				case ETokenType.Camera: return       "camera";
				case ETokenType.SceneNode: return    "scene_node";
				case ETokenType.Input: return        "input";
				case ETokenType.Stereo: return       "stereo";
				case ETokenType.Network: return      "network";
				case ETokenType.Debug: return        "debug";
				case ETokenType.General: return      "general";
				case ETokenType.Custom: return       "custom";
			}

			return string.Empty;
		}

		public string GetTokenFullName(ETokenType type)
		{
			string name = GetTokenShortName(type);
			return string.Format("[{0}]", name);
		}

		protected List<KeyValuePair<ETokenType, string>> ConfigLines = new List<KeyValuePair<ETokenType, string>>();

		protected bool LoadContent(string FilePath)
		{
			string[] AllLines = File.ReadAllLines(FilePath);
			foreach (string DirtyLine in AllLines)
			{
				string CleanLine = DirtyLine.Trim();
				ConfigLines.Add(new KeyValuePair<ETokenType, string>(GetToken(CleanLine), CleanLine));
			}

			return true;
		}

		protected bool SaveConfig(string FilePath)
		{
			string[] AllStrings = (from kvp in ConfigLines select kvp.Value).ToArray();
			File.WriteAllLines(FilePath, AllStrings);
			return true;
		}

		public static ETokenType GetToken(string ConfigLine)
		{
			string Line = ConfigLine.Trim().ToLower();
			if (Line.StartsWith("[info]"))
			{
				return ETokenType.Info;
			}
			else if (Line.StartsWith("[cluster_node]"))
			{
				return ETokenType.ClusterNode;
			}
			else if (Line.StartsWith("[window]"))
			{
				return ETokenType.Window;
			}
			else if (Line.StartsWith("[screen]"))
			{
				return ETokenType.Screen;
			}
			else if (Line.StartsWith("[viewport]"))
			{
				return ETokenType.Viewport;
			}
			else if (Line.StartsWith("[projection]"))
			{
				return ETokenType.Projection;
			}
			else if (Line.StartsWith("[camera]"))
			{
				return ETokenType.Camera;
			}
			else if (Line.StartsWith("[scene_node]"))
			{
				return ETokenType.SceneNode;
			}
			else if (Line.StartsWith("[input]"))
			{
				return ETokenType.Input;
			}
			else if (Line.StartsWith("[stereo]"))
			{
				return ETokenType.Stereo;
			}
			else if (Line.StartsWith("[network]"))
			{
				return ETokenType.Network;
			}
			else if (Line.StartsWith("[debug]"))
			{
				return ETokenType.Debug;
			}
			else if (Line.StartsWith("[general]"))
			{
				return ETokenType.General;
			}
			else if (Line.StartsWith("[custom]"))
			{
				return ETokenType.Custom;
			}

			return ETokenType.Other;
		}
	}
}
