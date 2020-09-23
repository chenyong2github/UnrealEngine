// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Globalization;
using Tools.DotNETCommon;
using AutomationTool;
using UnrealBuildTool;

namespace Turnkey
{
	static class TurnkeyUtils
	{
		// object that commands, etc can use to access UAT functionality
		static public BuildCommand CommandUtilHelper;

		// replacement for Environment.ExitCode
		static public AutomationTool.ExitCode ExitCode = ExitCode.Success;

		static public void Initialize(IOProvider InIOProvider, BuildCommand InCommandUtilHelper)
		{
			IOProvider = InIOProvider;
			CommandUtilHelper = InCommandUtilHelper;

			// set up some lists
			SetVariable("AllPlatforms", string.Join(",", UnrealTargetPlatform.GetValidPlatformNames()));

			// walk over all the SDKs and get their AutoSDK string
			IEnumerable<string> AutoSDKPlatforms = UEBuildPlatformSDK.AllSDKs.Select(x => x.GetAutoSDKPlatformName()).Distinct();
			SetVariable("AutoSDKPlatforms", string.Join(",", AutoSDKPlatforms));

// 			TurnkeyUtils.Log("AllPlatforms = {0}", GetVariableValue("AllPlatforms"));
// 			TurnkeyUtils.Log("AutoSDKPlatforms = {0}", GetVariableValue("AutoSDKPlatforms"));

			SetVariable("HOST_PLATFORM_NAME", HostPlatform.Current.HostEditorPlatform.ToString());
		}

		#region Turnkey Variables

		static Dictionary<string, string> TurnkeyVariables = new Dictionary<string, string>();

		public static string SetVariable(string Key, string Value)
		{
			string Previous;
			TurnkeyVariables.TryGetValue(Key, out Previous);
			TurnkeyVariables[Key] = Value;
			return Previous;
		}
		public static string GetVariableValue(string Key)
		{
			string Value;
			if (TurnkeyVariables.TryGetValue(Key, out Value) || TurnkeySettings.GetSetUserSettings().TryGetValue(Key, out Value))
			{
				return Value;
			}
			return null;
		}

		public static void ClearVariable(string Key)
		{
			TurnkeyVariables.Remove(Key);
		}
		public static bool HasVariable(string Key)
		{
			return TurnkeyVariables.ContainsKey(Key);
		}

		public static string ExpandVariables(string Str, bool bUseOnlyTurnkeyVariables=false)
		{
			// don't crash on null
			if (Str == null)
			{
				return null;
			}

			string ExpandedUserVariables = UnrealBuildTool.Utils.ExpandVariables(Str, TurnkeySettings.GetSetUserSettings(), true);
			return UnrealBuildTool.Utils.ExpandVariables(ExpandedUserVariables, TurnkeyVariables, bUseOnlyTurnkeyVariables);
		}

		#endregion

		#region Commandline Handling

		public static bool ParseParam(string Param, string[] ExtraOptions)
		{
			// our internal extraoptions still have - in front, but CommandUtilHelper won't have the dashes
			return CommandUtils.ParseParam(ExtraOptions, "-" + Param) || CommandUtilHelper.ParseParam(Param);
		}

		public static string ParseParamValue(string Param, string Default, string[] ExtraOptions)
		{
			// our internal extraoptions still have - in front, but CommandUtilHelper won't have the dashes
			string Value = CommandUtils.ParseParamValue(ExtraOptions, "-" + Param, Default); 
			if (Value == null)
			{
				Value = CommandUtilHelper.ParseParamValue(Param, Default);
			}

			return Value == null ? Default : Value;
		}

		public static List<UnrealTargetPlatform> GetPlatformsFromCommandLineOrUser(string[] CommandOptions, List<UnrealTargetPlatform> PossiblePlatforms)
		{
			string PlatformString = TurnkeyUtils.ParseParamValue("Platform", null, CommandOptions);
			bool bUnattended = TurnkeyUtils.ParseParam("Unattended", CommandOptions);

			// HACK UNTIL WIN32 IS GONE
			PossiblePlatforms = PossiblePlatforms.Where(x => x != UnrealTargetPlatform.Win32 && x != UnrealTargetPlatform.XboxOne).ToList();

			List<UnrealTargetPlatform> Platforms = new List<UnrealTargetPlatform>();
			// prompt user for a platform
			if (PlatformString == null)
			{
				if (bUnattended)
				{
					// can't ask
					return null;
				}

				List<string> PlatformOptions = PossiblePlatforms.ConvertAll(x => x.ToString());
				PlatformOptions.Add("All of the Above");
				int PlatformChoice = TurnkeyUtils.ReadInputInt("Choose a platform:", PlatformOptions, true);

				if (PlatformChoice == 0)
				{
					return null;
				}
				// All platforms means to install every platform with an installer
				if (PlatformChoice == PlatformOptions.Count)
				{
					Platforms = PossiblePlatforms;
				}
				else
				{
					Platforms.Add(PossiblePlatforms[PlatformChoice - 1]);
				}
			}
			else if (PlatformString.ToLower() == "all")
			{
				Platforms = PossiblePlatforms;
			}
			else
			{
				string[] Tokens = PlatformString.Split("+".ToCharArray(), StringSplitOptions.RemoveEmptyEntries);

				foreach (string Token in Tokens)
				{
					UnrealTargetPlatform Platform;
					if (!UnrealTargetPlatform.TryParse(Token, out Platform))
					{
						TurnkeyUtils.Log("Platform {0} is unknown", Token);
						continue;
					}

					// if the platform isn't in the possible list, then don't add it
					if (PossiblePlatforms.Contains(Platform))
					{
						Platforms.Add(Platform);
					}
				}
			}

			return Platforms.OrderBy(x => x.ToString()).ToList();
		}

		public static DeviceInfo GetDeviceFromCommandLineOrUser(string[] CommandOptions, UnrealTargetPlatform Platform)
		{
			string DeviceName = TurnkeyUtils.ParseParamValue("Device", null, CommandOptions);

			AutomationTool.Platform AutomationPlatform = AutomationTool.Platform.Platforms[new TargetPlatformDescriptor(Platform)];
			
			if (DeviceName == null)
			{
				List<string> Options = new List<string>();
				// @todo turnkey: filter devices that have valid flash versions
				//				DeviceInfo[] PossibleDevices = Array.FindAll(AutomationPlatform.GetDevices(), x => TurnkeyUtils.IsValueValid(x.SoftwareVersion, AutomationPlatform.GetAllowedSoftwareVersions(), AutomationPlatforms[Platform]));
				DeviceInfo[] PossibleDevices = AutomationPlatform.GetDevices();
				if (PossibleDevices == null)
				{
					return null;
				}

				foreach (DeviceInfo Device in PossibleDevices)
				{
					Options.Add(string.Format("[{0} {1}] {2}", Platform, Device.Type, Device.Name));
				}

				// get the choice
				int Choice = TurnkeyUtils.ReadInputInt("Select the number of a device to flash:", Options, true);

				if (Choice == 0)
				{
					return null;
				}

				// get the name of the device chosen
				DeviceName = PossibleDevices[Choice - 1].Name;
			}

			// get device info of the chosen or supplied device
			DeviceInfo InstallDevice = Array.Find(AutomationPlatform.GetDevices(), x => string.Compare(x.Name, DeviceName, true) == 0);
			return InstallDevice;
		}
		#endregion

		#region Env vars

		private static IDictionary[] SavedEnvVars = new System.Collections.IDictionary[2];
		private static Dictionary<string, string> EnvVarsToSaveToBatchFile = new Dictionary<string, string>();

		public static void StartTrackingExternalEnvVarChanges()
		{
			SavedEnvVars[0] = Environment.GetEnvironmentVariables(EnvironmentVariableTarget.User);
			SavedEnvVars[1] = Environment.GetEnvironmentVariables(EnvironmentVariableTarget.Machine);
		}

		public static void EndTrackingExternalEnvVarChanges()
		{
			System.Collections.IDictionary[] NewEnvVars =
			{
				Environment.GetEnvironmentVariables(EnvironmentVariableTarget.User),
				Environment.GetEnvironmentVariables(EnvironmentVariableTarget.Machine),
			};

			TurnkeyUtils.Log("Scanning for envvar changes...");

			// look for differences
			for (int Index = 0; Index < NewEnvVars.Length; Index++)
			{
				IDictionary NewSet = NewEnvVars[Index];
				IDictionary PreviousSet = SavedEnvVars[Index];

				foreach (DictionaryEntry Pair in NewSet)
				{
					Object PrevValue = PreviousSet[Pair.Key];
					string NewKey = Pair.Key as string;
					string NewValue= Pair.Value as string;

					// if we have a new or changed value, apply it to the process
					if (PrevValue == null || string.Compare(PrevValue as string, NewValue) != 0)
					{
						TurnkeyUtils.Log("  Updating process env var {0} to {1}", Pair.Key, Pair.Value);
						Environment.SetEnvironmentVariable(NewKey, NewValue, EnvironmentVariableTarget.Process);
						// remember to save to batch file
						EnvVarsToSaveToBatchFile[NewKey] = NewValue;
					}
				}
			}

			TurnkeyUtils.Log("... done! ");
			if (EnvVarsToSaveToBatchFile.Count > 0)
			{
				StringBuilder BatchContents = new StringBuilder();
				foreach (var Pair in EnvVarsToSaveToBatchFile)
				{
					BatchContents.AppendLine("set {0}={1}", Pair.Key, Pair.Value);
				}

				// write out a batch file for the caller of this to call to update vars, including all changes from previous commands
				string BatchPath = ExpandVariables("$(EngineDir)/Intermediate/Turnkey/PostTurnkeyVariables.bat");
				TurnkeyUtils.Log("  Writing updated envvars to {0}", BatchPath);
				Directory.CreateDirectory(Path.GetDirectoryName(BatchPath));
				File.WriteAllText(BatchPath, BatchContents.ToString());
			}
		}

		#endregion

		#region Regex Matching

		static bool TryConvertToUint64(string InValue, out UInt64 OutValue)
		{ 
			if (InValue.StartsWith("0x"))
			{
				// must skip ovr the 0x
				return UInt64.TryParse(InValue.Substring(2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out OutValue);
			}
			return UInt64.TryParse(InValue, out OutValue);
		}

		public static bool IsValueValid(string Value, string AllowedValues, AutomationTool.Platform Platform)
		{
			if (string.IsNullOrEmpty(Value))
			{
				return false;
			}
			if (string.IsNullOrEmpty(AllowedValues))
			{
				return true;
			}

			// use a regex if the allowed string starts with "regex:"
			if (AllowedValues.StartsWith("regex:", StringComparison.InvariantCultureIgnoreCase))
			{
				return Regex.IsMatch(Value, AllowedValues.Substring(6));
			}

			// range type needs to have values that can convert to an integer (in base 10 or 16 with 0x)
			if (AllowedValues.StartsWith("range:", StringComparison.InvariantCultureIgnoreCase))
			{
				// match for "[Min]-[Max]" ([] meaning optional), and Min or Max can be a hex or decimal number,
				// and it must match the entire string, so, 0-10.0 would not match
				Match StreamMatch = new Regex(@"^(0x[0-9a-fA-F]*|[0-9]*)-(0x[0-9a-fA-F]*|[0-9]*)$").Match(AllowedValues.Substring(6));

				if (!StreamMatch.Success)
				{
					TurnkeyUtils.Log("Warning: range: type [{0}] was in a bad format. Must be a range of one or two positive integers in decimal or hex (ex: '0x1000-0x2000', or '435-')");
					return false;
				}

				// convert inputs to uint (unless already converted above by platform)
				UInt64 ValueInt = 0;
				if (!TryConvertToUint64(Value, out ValueInt) && !UEBuildPlatformSDK.GetSDKForPlatform(Platform.PlatformType.ToString()).TryConvertVersionToInt(Value, out ValueInt))
				{
					TurnkeyUtils.Log("Warning: range: input value [{0}] was not an unsigned integer, and platform couldn't convert it", Value);
					return false;
				}

				// min and max are optional, so use 0 and MaxValue if they aren't
				string MinString = StreamMatch.Groups[1].Value;
				string MaxString = StreamMatch.Groups[2].Value;
				UInt64 Min = 0, Max = UInt64.MaxValue;
				if (!string.IsNullOrEmpty(MinString))
				{
					// Regex verified they are in a good format, so we can use Parse
					TryConvertToUint64(MinString, out Min);
				}
				if (!string.IsNullOrEmpty(MaxString))
				{
					TryConvertToUint64(MaxString, out Max);
				}

				// finally perform the comparison
				return ValueInt >= Min && ValueInt <= Max;
			}

			// otherwise, perform a string comparison
			return string.Compare(Value, AllowedValues, true) == 0;
		}

		#endregion

		#region IO
		static private IOProvider IOProvider;

		public static void Log(string Message)
		{
			IOProvider.Log(Message, bAppendNewLine: true);
		}
		public static void Log(ref StringBuilder Message)
		{
			IOProvider.Log(Message.ToString(), bAppendNewLine: false);
			Message.Clear();
		}
		public static void Log(string Message, params object[] Params)
		{
			IOProvider.Log(string.Format(Message, Params), bAppendNewLine: true);
		}

		public static void Report(string Message)
		{
			IOProvider.Report(Message, bAppendNewLine: true);
		}
		public static void Report(ref StringBuilder Message)
		{
			IOProvider.Report(Message.ToString(), bAppendNewLine: false);
			Message.Clear();
		}
		public static void Report(string Message, params object[] Params)
		{
			IOProvider.Report(string.Format(Message, Params), bAppendNewLine: true);
		}

		public static string ReadInput(string Prompt, string Default = "")
		{
			return IOProvider.ReadInput(Prompt, Default, bAppendNewLine: true);
		}
		public static int ReadInputInt(ref StringBuilder Prompt, List<string> Options, bool bIsCancellable, int DefaultValue = -1)
		{
			string PromptString = Prompt.ToString();
			Prompt.Clear();
			return IOProvider.ReadInputInt(PromptString, Options, bIsCancellable, DefaultValue, bAppendNewLine: false);
		}
		public static int ReadInputInt(string Prompt, List<string> Options, bool bIsCancellable, int DefaultValue = -1)
		{
			return IOProvider.ReadInputInt(Prompt, Options, bIsCancellable, DefaultValue, bAppendNewLine: true);
		}
		#endregion

		#region Temp files [move to LocalCache]
		static List<string> PathsToCleanup = new List<string>();
		public static void AddPathToCleanup(string Path)
		{
			PathsToCleanup.Add(Path);
		}

		public static void CleanupPaths()
		{
			TurnkeyUtils.Log("Cleaning Temp Paths...");

			// cleanup any delay-cleanup files and directories
			foreach (string Path in PathsToCleanup)
			{
				if (Directory.Exists(Path))
				{
					InternalUtils.SafeDeleteDirectory(Path);
				}
				else if (File.Exists(Path))
				{
					InternalUtils.SafeDeleteFile(Path);
				}
			}

			PathsToCleanup.Clear();
		}
		#endregion
	}
}
