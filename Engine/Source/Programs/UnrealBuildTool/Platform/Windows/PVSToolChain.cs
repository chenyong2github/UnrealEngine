// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Xml;
using System.Xml.Serialization;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// Partial representation of PVS-Studio main settings file
	/// </summary>
	[XmlRoot("ApplicationSettings")]
	public class PVSApplicationSettings
	{
		/// <summary>
		/// Masks for paths excluded for analysis
		/// </summary>
		public string[] PathMasks;

		/// <summary>
		/// Registered username
		/// </summary>
		public string UserName;

		/// <summary>
		/// Registered serial number
		/// </summary>
		public string SerialNumber;
	}

	/// <summary>
	/// Special mode for gathering all the messages into a single output file
	/// </summary>
	[ToolMode("PVSGather", ToolModeOptions.None)]
	class PVSGatherMode : ToolMode
	{
		/// <summary>
		/// Path to the input file list
		/// </summary>
		[CommandLine("-Input", Required = true)]
		FileReference InputFileList = null;

		/// <summary>
		/// Output file to generate
		/// </summary>
		[CommandLine("-Output", Required = true)]
		FileReference OutputFile = null;

		/// <summary>
		/// Execute the command
		/// </summary>
		/// <param name="Arguments">List of command line arguments</param>
		/// <returns>Always zero, or throws an exception</returns>
		public override int Execute(CommandLineArguments Arguments)
		{
			Arguments.ApplyTo(this);
			Arguments.CheckAllArgumentsUsed();

			Log.TraceInformation("{0}", OutputFile.GetFileName());

			// Read the input files
			string[] InputFileLines = FileReference.ReadAllLines(InputFileList);
			FileReference[] InputFiles = InputFileLines.Select(x => x.Trim()).Where(x => x.Length > 0).Select(x => new FileReference(x)).ToArray();

			// Create the combined output file, and print the diagnostics to the log
			HashSet<string> UniqueItems = new HashSet<string>();
			using (StreamWriter RawWriter = new StreamWriter(OutputFile.FullName))
			{
				foreach (FileReference InputFile in InputFiles)
				{
					string[] Lines = File.ReadAllLines(InputFile.FullName);
					for(int LineIdx = 0; LineIdx < Lines.Length; LineIdx++)
					{
						string Line = Lines[LineIdx];
						if (!String.IsNullOrWhiteSpace(Line) && UniqueItems.Add(Line))
						{
							bool bCanParse = false;

							string[] Tokens = Line.Split(new string[] { "<#~>" }, StringSplitOptions.None);
							if(Tokens.Length >= 9)
							{
								string Trial = Tokens[1];
								string LineNumberStr = Tokens[2];
								string FileName = Tokens[3];
								string WarningCode = Tokens[5];
								string WarningMessage = Tokens[6];
								string FalseAlarmStr = Tokens[7];
								string LevelStr = Tokens[8];

								int LineNumber;
								bool bFalseAlarm;
								int Level;
								if(int.TryParse(LineNumberStr, out LineNumber) && bool.TryParse(FalseAlarmStr, out bFalseAlarm) && int.TryParse(LevelStr, out Level))
								{
									bCanParse = true;

									// Ignore anything in ThirdParty folders
									if(FileName.Replace('/', '\\').IndexOf("\\ThirdParty\\", StringComparison.InvariantCultureIgnoreCase) == -1)
									{
										// Output the line to the raw output file
										RawWriter.WriteLine(Line);

										// Output the line to the log
										if (!bFalseAlarm && Level == 1)
										{
											Log.WriteLine(LogEventType.Warning, LogFormatOptions.NoSeverityPrefix, "{0}({1}): warning {2}: {3}", FileName, LineNumber, WarningCode, WarningMessage);
										}
									}
								}
							}

							if(!bCanParse)
							{
								Log.WriteLine(LogEventType.Warning, LogFormatOptions.NoSeverityPrefix, "{0}({1}): warning: Unable to parse PVS output line '{2}' (tokens=|{3}|)", InputFile, LineIdx + 1, Line, String.Join("|", Tokens));
							}
						}
					}
				}
			}
			Log.TraceInformation("Written {0} {1} to {2}.", UniqueItems.Count, (UniqueItems.Count == 1)? "diagnostic" : "diagnostics", OutputFile.FullName);
			return 0;
		}
	}

	class PVSToolChain : UEToolChain
	{
		ReadOnlyTargetRules Target;
		VCToolChain InnerToolChain;
		FileReference AnalyzerFile;
		FileReference LicenseFile;
		PVSApplicationSettings ApplicationSettings;
		UnrealTargetPlatform Platform;

		public PVSToolChain(ReadOnlyTargetRules Target)
		{
			this.Target = Target;
			Platform = Target.Platform;
			InnerToolChain = new VCToolChain(Target);

			AnalyzerFile = FileReference.Combine(new DirectoryReference(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86)), "PVS-Studio", "x64", "PVS-Studio.exe");
			if(!FileReference.Exists(AnalyzerFile))
			{
				FileReference EngineAnalyzerFile = FileReference.Combine(UnrealBuildTool.RootDirectory, "Engine", "Extras", "ThirdPartyNotUE", "NoRedist", "PVS-Studio", "PVS-Studio.exe");
				if (FileReference.Exists(EngineAnalyzerFile))
				{
					AnalyzerFile = EngineAnalyzerFile;
				}
				else
				{
					throw new BuildException("Unable to find PVS-Studio at {0} or {1}", AnalyzerFile, EngineAnalyzerFile);
				}
			}

			FileReference SettingsPath = FileReference.Combine(new DirectoryReference(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData)), "PVS-Studio", "Settings.xml");
			if (FileReference.Exists(SettingsPath))
			{
				try
				{
					XmlSerializer Serializer = new XmlSerializer(typeof(PVSApplicationSettings));
					using(FileStream Stream = new FileStream(SettingsPath.FullName, FileMode.Open, FileAccess.Read, FileShare.Read))
					{
						ApplicationSettings = (PVSApplicationSettings)Serializer.Deserialize(Stream);
					}
				}
				catch(Exception Ex)
				{
					throw new BuildException(Ex, "Unable to read PVS-Studio settings file from {0}", SettingsPath);
				}
			}

			if(ApplicationSettings != null && !String.IsNullOrEmpty(ApplicationSettings.UserName) && !String.IsNullOrEmpty(ApplicationSettings.SerialNumber))
			{
				LicenseFile = FileReference.Combine(UnrealBuildTool.EngineDirectory, "Intermediate", "PVS", "PVS-Studio.lic");
				FileItem.CreateIntermediateTextFile(LicenseFile, new string[]{ ApplicationSettings.UserName, ApplicationSettings.SerialNumber });
			}
			else
			{
				FileReference DefaultLicenseFile = AnalyzerFile.ChangeExtension(".lic");
				if(FileReference.Exists(DefaultLicenseFile))
				{
					LicenseFile = DefaultLicenseFile;
				}
			}
		}

		static string GetFullIncludePath(string IncludePath)
		{
			return Path.GetFullPath(Utils.ExpandVariables(IncludePath));
		}

		public override CPPOutput CompileCPPFiles(CppCompileEnvironment CompileEnvironment, List<FileItem> InputFiles, DirectoryReference OutputDir, string ModuleName, List<Action> Actions)
		{
			// Preprocess the source files with the regular toolchain
			CppCompileEnvironment PreprocessCompileEnvironment = new CppCompileEnvironment(CompileEnvironment);
			PreprocessCompileEnvironment.bPreprocessOnly = true;
			PreprocessCompileEnvironment.bEnableUndefinedIdentifierWarnings = false; // Not sure why THIRD_PARTY_INCLUDES_START doesn't pick this up; the _Pragma appears in the preprocessed output. Perhaps in preprocess-only mode the compiler doesn't respect these?
			PreprocessCompileEnvironment.Definitions.Add("PVS_STUDIO");

			List<Action> PreprocessActions = new List<Action>();
			CPPOutput Result = InnerToolChain.CompileCPPFiles(PreprocessCompileEnvironment, InputFiles, OutputDir, ModuleName, PreprocessActions);
			Actions.AddRange(PreprocessActions);

			// Run the source files through PVS-Studio
			foreach(Action PreprocessAction in PreprocessActions)
			{
				if (PreprocessAction.ActionType != ActionType.Compile)
				{
					continue;
				}

				FileItem SourceFileItem = PreprocessAction.PrerequisiteItems.FirstOrDefault(x => x.HasExtension(".c") || x.HasExtension(".cc") || x.HasExtension(".cpp"));
				if (SourceFileItem == null)
				{
					Log.TraceWarning("Unable to find source file from command: {0} {1}", PreprocessAction.CommandArguments);
					continue;
				}

				FileItem PreprocessedFileItem = PreprocessAction.ProducedItems.FirstOrDefault(x => x.HasExtension(".i"));
				if (PreprocessedFileItem == null)
				{
					Log.TraceWarning("Unable to find preprocessed output file from command: {0} {1}", PreprocessAction.CommandArguments);
					continue;
				}

				// Disable a few warnings that seem to come from the preprocessor not respecting _Pragma
				PreprocessAction.CommandArguments += " /wd4005"; // macro redefinition
				PreprocessAction.CommandArguments += " /wd4828"; // file contains a character starting at offset xxxx that is illegal in the current source character set

				// Write the PVS studio config file
				StringBuilder ConfigFileContents = new StringBuilder();
				foreach(DirectoryReference IncludePath in Target.WindowsPlatform.Environment.IncludePaths)
				{
					ConfigFileContents.AppendFormat("exclude-path={0}\n", IncludePath.FullName);
				}
				if(ApplicationSettings != null && ApplicationSettings.PathMasks != null)
				{
					foreach(string PathMask in ApplicationSettings.PathMasks)
					{
						if (PathMask.Contains(":") || PathMask.Contains("\\") || PathMask.Contains("/"))
						{
							if(Path.IsPathRooted(PathMask) && !PathMask.Contains(":"))
							{
								ConfigFileContents.AppendFormat("exclude-path=*{0}*\n", PathMask);
							}
							else
							{
								ConfigFileContents.AppendFormat("exclude-path={0}\n", PathMask);
							}
						}
					}
				}
				if (Platform == UnrealTargetPlatform.Win64)
				{
					ConfigFileContents.Append("platform=x64\n");
				}
				else if(Platform == UnrealTargetPlatform.Win32)
				{
					ConfigFileContents.Append("platform=Win32\n");
				}
				else
				{
					throw new BuildException("PVS-Studio does not support this platform");
				}
				ConfigFileContents.Append("preprocessor=visualcpp\n");
				ConfigFileContents.Append("language=C++\n");
				ConfigFileContents.Append("skip-cl-exe=yes\n");
				ConfigFileContents.AppendFormat("i-file={0}\n", PreprocessedFileItem.Location.FullName);

				string BaseFileName = PreprocessedFileItem.Location.GetFileNameWithoutExtension();

				FileReference ConfigFileLocation = FileReference.Combine(OutputDir, BaseFileName + ".cfg");
				FileItem ConfigFileItem = FileItem.CreateIntermediateTextFile(ConfigFileLocation, ConfigFileContents.ToString());

				// Run the analzyer on the preprocessed source file
				FileReference OutputFileLocation = FileReference.Combine(OutputDir, BaseFileName + ".pvslog");
				FileItem OutputFileItem = FileItem.GetItemByFileReference(OutputFileLocation);

				Action AnalyzeAction = new Action(ActionType.Compile);
				AnalyzeAction.CommandDescription = "Analyzing";
				AnalyzeAction.StatusDescription = BaseFileName;
				AnalyzeAction.WorkingDirectory = UnrealBuildTool.EngineSourceDirectory;
				AnalyzeAction.CommandPath = AnalyzerFile;
				AnalyzeAction.CommandArguments = String.Format("--cl-params \"{0}\" --source-file \"{1}\" --output-file \"{2}\" --cfg \"{3}\" --analysis-mode 4", PreprocessAction.CommandArguments, SourceFileItem.AbsolutePath, OutputFileLocation, ConfigFileItem.AbsolutePath);
				if (LicenseFile != null)
				{
					AnalyzeAction.CommandArguments += String.Format(" --lic-file \"{0}\"", LicenseFile);
					AnalyzeAction.PrerequisiteItems.Add(FileItem.GetItemByFileReference(LicenseFile));
				}
				AnalyzeAction.PrerequisiteItems.Add(ConfigFileItem);
				AnalyzeAction.PrerequisiteItems.Add(PreprocessedFileItem);
				AnalyzeAction.ProducedItems.Add(OutputFileItem);
				AnalyzeAction.DeleteItems.Add(OutputFileItem); // PVS Studio will append by default, so need to delete produced items
				Actions.Add(AnalyzeAction);

				Result.ObjectFiles.AddRange(AnalyzeAction.ProducedItems);
			}
			return Result;
		}

		public override FileItem LinkFiles(LinkEnvironment LinkEnvironment, bool bBuildImportLibraryOnly, List<Action> Actions)
		{
			throw new BuildException("Unable to link with PVS toolchain.");
		}

		public override void FinalizeOutput(ReadOnlyTargetRules Target, TargetMakefile Makefile)
		{
			FileReference OutputFile;
			if (Target.ProjectFile == null)
			{
				OutputFile = FileReference.Combine(UnrealBuildTool.EngineDirectory, "Saved", "PVS-Studio", String.Format("{0}.pvslog", Target.Name));
			}
			else
			{
				OutputFile = FileReference.Combine(Target.ProjectFile.Directory, "Saved", "PVS-Studio", String.Format("{0}.pvslog", Target.Name));
			}

			List<FileReference> InputFiles = Makefile.OutputItems.Select(x => x.Location).Where(x => x.HasExtension(".pvslog")).ToList();

			FileItem InputFileListItem = FileItem.CreateIntermediateTextFile(OutputFile.ChangeExtension(".input"), InputFiles.Select(x => x.FullName));

			Action AnalyzeAction = new Action(ActionType.Compile);
			AnalyzeAction.CommandPath = UnrealBuildTool.GetUBTPath();
			AnalyzeAction.CommandArguments = String.Format("-Mode=PVSGather -Input=\"{0}\" -Output=\"{1}\"", InputFileListItem.Location, OutputFile);
			AnalyzeAction.WorkingDirectory = UnrealBuildTool.EngineSourceDirectory;
			AnalyzeAction.PrerequisiteItems.Add(InputFileListItem);
			AnalyzeAction.PrerequisiteItems.AddRange(Makefile.OutputItems);
			AnalyzeAction.ProducedItems.Add(FileItem.GetItemByFileReference(OutputFile));
			AnalyzeAction.DeleteItems.AddRange(AnalyzeAction.ProducedItems);
			Makefile.Actions.Add(AnalyzeAction);

			Makefile.OutputItems.AddRange(AnalyzeAction.ProducedItems);
		}
	}
}
