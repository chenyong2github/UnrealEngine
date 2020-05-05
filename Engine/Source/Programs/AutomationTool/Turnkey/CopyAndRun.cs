using System;
using System.Xml.Serialization;
using System.IO;
using UnrealBuildTool;
using AutomationTool;
using Tools.DotNETCommon;
using System.Diagnostics;
using System.Security.Permissions;
using System.Security.Principal;

namespace Turnkey
{
	public class CopyAndRun
	{
		// @todo turnkey: for some reason the TypeConverter stuff setup in UnrealTargetPlatform isn't kicking in to convert from string to UTP, so do it a manual way
		[XmlElement("HostPlatform")]
		public string PlatformString = null;

		[XmlIgnore]
		public UnrealTargetPlatform? Platform = null;

		public string Copy = null;

		public string CommandPath = null;

		public string CommandLine = null;

		/// <summary>
		/// Needs a parameterless constructor for Xml deserialization
		/// </summary>
		CopyAndRun()
		{ }

		/// <summary>
		/// Create a one-of local object with just a Copy operation
		/// </summary>
		/// <param name="CopyOperation"></param>
		public CopyAndRun(string CopyOperation)
		{
			Copy = TurnkeyUtils.ExpandVariables(CopyOperation);
		}
		public CopyAndRun(CopyAndRun Other)
		{
			PlatformString = Other.PlatformString;
			Platform = Other.Platform;
			Copy = Other.Copy;
			CommandPath = Other.CommandPath;
			CommandLine = Other.CommandLine;
		}

		internal void PostDeserialize()
		{
			if (!string.IsNullOrEmpty(PlatformString))
			{
				Platform = UnrealTargetPlatform.Parse(PlatformString);
			}

			// perform early expansion, important for $(ThisManifestDir) which is valid only during deserialization
			// but don't use any other variables yet, because UAT could have bad values in Environment
			Copy = TurnkeyUtils.ExpandVariables(Copy, bUseOnlyTurnkeyVariables: true);
			CommandPath = TurnkeyUtils.ExpandVariables(CommandPath, bUseOnlyTurnkeyVariables: true);
			CommandLine = TurnkeyUtils.ExpandVariables(CommandLine, bUseOnlyTurnkeyVariables: true);
		}

		public bool Execute(CopyExecuteSpecialMode SpecialMode=CopyExecuteSpecialMode.None, string ModeHint=null)
		{
			if (ShouldExecute() == false)
			{
				return false;
			}

			// do the copy operation, capturing the output
			string OutputPath = null;
			if (Copy != null)
			{
				// if there was a copy operation do it
				OutputPath = CopyProvider.ExecuteCopy(Copy, SpecialMode, ModeHint);

				// if it returned null, then something went wrong, don't continue with the command. 
				// assume the CopyProvider supplied enough information, so we quietly skip
				if (OutputPath == null)
				{
					return false;
				}

				TurnkeyUtils.SetVariable("CopyOutputPath", OutputPath);

				if (SpecialMode == CopyExecuteSpecialMode.DownloadOnly)
				{
					// if we only wanted to download if needed, then we are done!
					return true;
				}
			}

			if ((CommandPath != null && CommandPath.Contains("$(CopyOutputPath)")) || (CommandLine != null && CommandLine.Contains("$(CopyOutputPath)")))
			{
				throw new AutomationException("CopyAndRun required an valid $(CopyOutputPath) from the Copy operation, but it failed");
			}

			// run an external command
			if (CommandPath != null)
			{
				string FixedCommandPath = TurnkeyUtils.ExpandVariables(CommandPath);
				string FixedCommandLine = TurnkeyUtils.ExpandVariables(CommandLine);
				FixedCommandPath = FixedCommandPath.Replace(Path.AltDirectorySeparatorChar, Path.DirectorySeparatorChar);

				string PreviousCWD = Environment.CurrentDirectory;

				TurnkeyUtils.StartTrackingExternalEnvVarChanges();

				// run in a loop in case of a failure
				bool bDone = false;
				while (!bDone)
				{
					// if a directory was included in the command path, then run from there
					string CommandWorkingDir = Path.GetDirectoryName(FixedCommandPath);
					if (!string.IsNullOrEmpty(CommandWorkingDir))
					{
						Environment.CurrentDirectory = CommandWorkingDir;
					}

					ProcessStartInfo ProcInfo = new ProcessStartInfo(FixedCommandPath, FixedCommandLine);
					int ExitCode = Utils.RunLocalProcessAndLogOutput(ProcInfo);
					if (ExitCode != 0)
					{
						TurnkeyUtils.Log("Command {0} {1} failed [Exit code {2}]", FixedCommandPath, FixedCommandLine, ExitCode);
						string Response = TurnkeyUtils.ReadInput("Do you want to attempt again? [y/N]", "N");
						if (string.Compare(Response, "Y", true) != 0)
						{
							bDone = true;
						}
					}
					else
					{
						bDone = true;
					}
				}

				TurnkeyUtils.EndTrackingExternalEnvVarChanges();

				Environment.CurrentDirectory = PreviousCWD;
			}

			return true;
		}

		public bool ShouldExecute()
		{
			return !Platform.HasValue || Platform == HostPlatform.Current.HostEditorPlatform;
		}
	}
}
