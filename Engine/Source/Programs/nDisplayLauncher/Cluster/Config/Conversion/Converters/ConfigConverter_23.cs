// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


namespace nDisplayLauncher.Cluster.Config.Conversion.Converters
{
	class ConfigConverter_23 : ConfigConverterBase, IConfigConverter
	{
		List<KeyValuePair<ETokenType, string>> LinesToAdd = new List<KeyValuePair<ETokenType, string>>();

		public bool Convert(string FileOld, string FileNew)
		{
			return LoadContent(FileOld)
				&& UpdateEntityInfo()
				&& MoveEyeParametersToCamerasEntity()
				&& IntroduceEntityProjection()
				&& Finalize()
				&& SaveConfig(FileNew);
		}

		private bool UpdateEntityInfo()
		{
			for (int i = 0; i < ConfigLines.Count; ++i)
			{
				if (ConfigLines[i].Key == ETokenType.Info)
				{
					ConfigLines[i] = new KeyValuePair<ETokenType, string>(ETokenType.Info, Parser.SetValue<int>(ConfigLines[i].Value, "version", 23));
				}
			}

			return true;
		}

		private bool MoveEyeParametersToCamerasEntity()
		{
			List<int> Cameras = new List<int>();
			float EyeDist = 0.064f;
			bool EyeSwap = false;

			const string TokEyeSwap = "eye_swap";
			const string TokEyeDist = "eye_dist";
			const string TokForceOffset = "force_offset";

			for (int i = 0; i < ConfigLines.Count; ++i)
			{
				// Find all cameras in config
				if (ConfigLines[i].Key == ETokenType.Camera)
				{
					Cameras.Add(i);
				}

				// Store & remove eye_swap
				if (ConfigLines[i].Key == ETokenType.ClusterNode)
				{
					EyeSwap = Parser.GetBoolValue(ConfigLines[i].Value, TokEyeSwap);
					ConfigLines[i] = new KeyValuePair<ETokenType, string>(ETokenType.ClusterNode, Parser.RemoveArgument(ConfigLines[i].Value, TokEyeSwap));
				}

				// Store & remove eye_dist
				if (ConfigLines[i].Key == ETokenType.Stereo)
				{
					EyeDist = Parser.GetFloatValue(ConfigLines[i].Value, TokEyeDist);
					ConfigLines[i] = new KeyValuePair<ETokenType, string>(ETokenType.Stereo, Parser.RemoveArgument(ConfigLines[i].Value, TokEyeDist));
				}
			}

			// Update all [camera] entities
			foreach (int idx in Cameras)
			{
				string NewCameraLine = ConfigLines[idx].Value;

				NewCameraLine = Parser.SetValue(NewCameraLine, TokEyeSwap, EyeSwap ? "true" : "false");
				NewCameraLine = Parser.SetValue(NewCameraLine, TokEyeDist, EyeDist);
				NewCameraLine = Parser.SetValue(NewCameraLine, TokForceOffset, 0);

				ConfigLines[idx] = new KeyValuePair<ETokenType, string>(ETokenType.Camera, NewCameraLine);
			}

			return true;
		}

		private bool IntroduceEntityProjection()
		{
			// Token constants
			const string TokScreen     = "screen";
			const string TokProjection = "projection";

			for (int i = 0; i < ConfigLines.Count; ++i)
			{
				if (ConfigLines[i].Key == ETokenType.Viewport)
				{
					string ScreenId = Parser.GetStringValue(ConfigLines[i].Value, TokScreen);
					string ProjId = "proj_" + ScreenId;

					string NewViewportLine = Parser.RemoveArgument(ConfigLines[i].Value, TokScreen);
					NewViewportLine = Parser.SetValue(NewViewportLine, TokProjection, ProjId);
					ConfigLines[i] = new KeyValuePair<ETokenType, string>(ETokenType.Viewport, NewViewportLine);
					LinesToAdd.Add(new KeyValuePair<ETokenType, string>(ETokenType.Projection, string.Format("{0} id=\"{1}\" type=\"simple\" screen=\"{2}\"", GetTokenFullName(ETokenType.Projection), ProjId, ScreenId)));
				}
			}

			return true;
		}

		private bool Finalize()
		{
			LinesToAdd.Add(new KeyValuePair<ETokenType, string>(ETokenType.Other, "# AUTO_CONVERSION, new entities finish"));
			LinesToAdd.Add(new KeyValuePair<ETokenType, string>(ETokenType.Other, string.Empty));
			LinesToAdd.Add(new KeyValuePair<ETokenType, string>(ETokenType.Other, string.Empty));

			ConfigLines.InsertRange(0, LinesToAdd);

			return true;
		}
	}
}
