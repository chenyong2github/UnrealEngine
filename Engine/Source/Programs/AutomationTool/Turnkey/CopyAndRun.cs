using System;
using System.Xml.Serialization;
using System.IO;
using UnrealBuildTool;
using AutomationTool;
using Tools.DotNETCommon;
using System.Diagnostics;
using System.ComponentModel;

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

			if (OutputPath == null && ((CommandPath != null && CommandPath.Contains("$(CopyOutputPath)")) || (CommandLine != null && CommandLine.Contains("$(CopyOutputPath)"))))
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
				// if a directory was included in the command path, then run from there
				string CommandWorkingDir = Path.GetDirectoryName(FixedCommandPath);

				// if we run UseShellExecute with true, StartInfo.WorkingDirectory won't set the directory as expected
				if (!string.IsNullOrEmpty(CommandWorkingDir))
				{
					Environment.CurrentDirectory = CommandWorkingDir;
				}


				TurnkeyUtils.StartTrackingExternalEnvVarChanges();

				// run installer as administrator, as some need it
				Process InstallProcess = new Process();
				InstallProcess.StartInfo.UseShellExecute = false;
				InstallProcess.StartInfo.FileName = FixedCommandPath;
				InstallProcess.StartInfo.Arguments = FixedCommandLine;
				InstallProcess.StartInfo.WindowStyle = ProcessWindowStyle.Hidden;

  				InstallProcess.OutputDataReceived += (Sender, Args) => { if (Args != null && Args.Data != null) TurnkeyUtils.Log(Args.Data.TrimEnd()); };
  				InstallProcess.ErrorDataReceived += (Sender, Args) => { if (Args != null && Args.Data != null) TurnkeyUtils.Log("Error: {0}", Args.Data.TrimEnd()); };

				//installers may require administrator access to succeed. so run as an admmin.

				// run in a loop in case of a failure
				bool bDone = false;
				int ExitCode;

				while (!bDone)
				{
					try
					{
						InstallProcess.Start();
						InstallProcess.WaitForExit();
						ExitCode = InstallProcess.ExitCode;
					}
					catch (Exception Ex)
					{
						// native error in a Win32Exception, of 740, means the process needs elevation, so we need to runas. However,
						// this will not allow capturing stdout, so run with window as Normal
						if (InstallProcess.StartInfo.UseShellExecute == false && Ex is Win32Exception && ((Win32Exception)Ex).NativeErrorCode == 740)
						{
							InstallProcess.StartInfo.UseShellExecute = true;
							InstallProcess.StartInfo.Verb = "runas";
							InstallProcess.StartInfo.WindowStyle = ProcessWindowStyle.Normal;

							TurnkeyUtils.Log("The installer {0} needed to run with elevated permissions, trying with Admin privileges (output may be hidden)", FixedCommandPath);

							// try again
							continue;
						}

						TurnkeyUtils.Log("Error: {0} caused an exception: {1}", FixedCommandPath, Ex.Message);
						ExitCode = -1;
					}

					if (ExitCode != 0)
					{
						TurnkeyUtils.Log("");
						TurnkeyUtils.Log("Command {0} {1} failed [Exit code {2}, working dir = {3}]", FixedCommandPath, FixedCommandLine, ExitCode, CommandWorkingDir);
						TurnkeyUtils.Log("");

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
