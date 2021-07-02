// Copyright Epic Games, Inc. All Rights Reserved.

using EnvDTE;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;
using Microsoft.VisualStudio.Text.Differencing;
using System;
using System.Collections.Generic;
using System.ComponentModel.Design;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;

namespace UnrealVS
{
	internal class P4Commands : IDisposable
	{
		private const bool PullWorkingDirectoryOn = true;
		private const bool PullWorkingDirectoryOff = false;

		private const bool OutputStdOutOn = true;
		private const bool OutputStdOutOff = false;

		private const int P4SubMenuID = 0x3100;
		private const int P4CheckoutButtonID = 0x1450;
		private const int P4AnnotateButtonID = 0x1451;
		private const int P4ViewSelectedCLButtonID = 0x1452;
		private const int P4IntegrationAwareTimelapseButtonID = 0x1453;
		private const int P4DiffinVSButtonID = 0x1454;



		private OleMenuCommand SubMenuCommand;

		private System.Diagnostics.Process ChildProcess;
		private IVsOutputWindowPane P4OutputPane;
		private string P4WorkingDirectory;
		private bool PullWorkingDirectorFromP4 = true;

		// stdXX from last operation
		private string P4OperationStdOut;
		private string P4OperationStdErr;

		// Exe paths
		private string P4Exe = "C:\\Program Files\\Perforce\\p4.exe";
		private string P4VCCmd = "C:\\Program Files\\Perforce\\p4vc.exe";
		private string P4VCCmdBat = "C:\\Program Files\\Perforce\\p4vc.bat";
		private string P4VExe = "C:\\Program Files\\Perforce\\p4v.exe";

		// user info
		private string Username = "";
		private string Port = "";
		private string Client = "";
		private string UserInfoComplete = "";

		private List<CommandEvents> EventsForce = new List<CommandEvents>();

		private class P4Command
		{
			public MenuCommand ButtonCommand;
			public CommandID CommandID;
			public P4Command(int ButtonID, EventHandler ButtonHandler)
			{
				CommandID = new CommandID(GuidList.UnrealVSCmdSet, ButtonID);
				ButtonCommand = new MenuCommand(new EventHandler(ButtonHandler), CommandID);

				UnrealVSPackage.Instance.MenuCommandService.AddCommand(ButtonCommand);
			}

			public void Toggle(bool Enabled)
			{
				ButtonCommand.Visible = ButtonCommand.Enabled = Enabled;
			}
		}

		private List<P4Command> P4CommandsList = new List<P4Command>();

		private bool IsSolutionLoaded()
		{
			DTE DTE = UnrealVSPackage.Instance.DTE;

			return DTE.Solution.FileName.Length > 0;
		}

		private void OnQuickBuildSubMenuQuery(object sender, EventArgs e)
		{
			DTE DTE = UnrealVSPackage.Instance.DTE;

			bool EnableCommands = UnrealVSPackage.Instance.OptionsPage.AllowUnrealVSP4;
			bool SolutionLoaded = IsSolutionLoaded();

			var SenderSubMenuCommand = (OleMenuCommand)sender;

			SenderSubMenuCommand.Visible = SenderSubMenuCommand.Enabled = EnableCommands & SolutionLoaded;
		}

		public P4Commands()
		{
			// setup callbacks on IDE operations
			UnrealVSPackage.Instance.OptionsPage.OnOptionsChanged += OnOptionsChanged;
			UnrealVSPackage.Instance.OnSolutionOpened += SoltuionOpened;
			UnrealVSPackage.Instance.OnSolutionClosed += SoltuionClosed;

			// create specific output window for unrealvs.P4
			P4OutputPane = UnrealVSPackage.Instance.GetP4OutputPane();

			// figure out the P4VC path
			if (!File.Exists(P4VCCmd))
			{
				if (File.Exists(P4VCCmd))
				{
					P4VCCmd = P4VCCmdBat;
				}
				else
				{
					P4OutputPane.Activate();
					P4OutputPane.OutputString($"1>------ P4VC not found, {P4VCCmd} or {P4VCCmd}{Environment.NewLine}");
					P4VCCmd = "";
				}

			}

			// add commands
			P4CommandsList.Add(new P4Command(P4CheckoutButtonID, P4CheckoutButtonHandler));
			P4CommandsList.Add(new P4Command(P4AnnotateButtonID, P4AnnotateButtonHandler));
			P4CommandsList.Add(new P4Command(P4IntegrationAwareTimelapseButtonID, P4IntegrationAwareTimeLapseHandler));
			P4CommandsList.Add(new P4Command(P4DiffinVSButtonID, P4DiffinVSHandler));

			if (P4VCCmd.Length > 1)
			{
				P4CommandsList.Add(new P4Command(P4ViewSelectedCLButtonID, P4ViewSelectedCLButtonHandler));

			}

			// add sub menu for commands
			SubMenuCommand = new OleMenuCommand(null, new CommandID(GuidList.UnrealVSCmdSet, P4SubMenuID));
			SubMenuCommand.BeforeQueryStatus += OnQuickBuildSubMenuQuery;
			UnrealVSPackage.Instance.MenuCommandService.AddCommand(SubMenuCommand);

			// Update the menu visibility to enforce user options.
			UpdateMenuOptions();

			// hook up the on "Save" list - internally they verify the users choice
			RegisterCallbackHandler("File.SaveSelectedItems", SaveSelectedCallback);
			RegisterCallbackHandler("File.SaveAll", OnSaveAll);

			// Hook up "Dirty" operations - things like 'build' when files are edited but not checked out hit this path
			RegisterCallbackHandler("Build.BuildSolution", SaveModifiedFiles);
			RegisterCallbackHandler("Build.Compile", SaveModifiedFiles);
			RegisterCallbackHandler("Build.BuildOnlyProject", SaveModifiedFiles);
			RegisterCallbackHandler("Debug.Start", SaveModifiedFiles);

			// there are *other* ways to hit the same operations - add those too.
			RegisterCallbackHandler("ClassViewContextMenus.ClassViewProject.Build", SaveModifiedFiles);
			RegisterCallbackHandler("ClassViewContextMenus.ClassViewProject.Rebuild", SaveModifiedFiles);
			RegisterCallbackHandler("ClassViewContextMenus.ClassViewProject.Debug.Startnewinstance", SaveModifiedFiles);
		}
		// Called when solutions are loaded or unloaded
		private void SoltuionOpened()
		{
			// Update the menu visibility
			UpdateMenuOptions();
		}

		private void SoltuionClosed()
		{
			// Update the menu visibility
			UpdateMenuOptions();

			// Clear any existing P4 working directory settings
			P4WorkingDirectory = "";
			PullWorkingDirectorFromP4 = true;
		}

		void RegisterCallbackHandler(string CommandName, _dispCommandEvents_BeforeExecuteEventHandler Callback)
		{
			// Find the command from the passed in name
			DTE DTE = UnrealVSPackage.Instance.DTE;
			CommandEvents Event = null;
			{
				// Probably should move this out to a function.
				try
				{
					Command Command = DTE.Commands.Item(CommandName, -1);
					if (Command != null)
					{
						Event = DTE.Events.get_CommandEvents(Command.Guid, Command.ID);
					}
				}
				catch
				{

				}
			}

			if (Event != null)
			{
				Event.BeforeExecute += Callback;
				EventsForce.Add(Event); // forces a reference
			}
		}

		private void SaveModifiedFiles(string Guid, int ID, object CustomIn, object CustomOut, ref bool CancelDefault)
		{
			if (UnrealVSPackage.Instance.OptionsPage.AllowUnrealVSCheckoutOnEdit)
			{
				foreach (Document File in UnrealVSPackage.Instance.DTE.Documents)
				{
					if (!File.Saved && File.ReadOnly)
					{
						OpenForEdit(File.FullName);
					}
				}
			}
		}

		void SaveSelectedCallback(string Guid, int ID, object CustomIn, object CustomOut, ref bool CancelDefault)
		{
			if (UnrealVSPackage.Instance.OptionsPage.AllowUnrealVSCheckoutOnEdit)
			{
				DTE DTE = UnrealVSPackage.Instance.DTE;

				foreach (SelectedItem Item in DTE.SelectedItems)
				{
					if (Item.Project != null)
					{
						OpenForEdit(Item.Project.FullName);
					}
					else if (Item.ProjectItem != null)
					{
						OpenForEdit(Item.ProjectItem.Document.FullName);
					}
					else
					{
						OpenForEdit(DTE.Solution.FullName);
					}
				}
			}
		}

		private void OnSaveAll(string Guid, int ID, object CustomIn, object CustomOut, ref bool CancelDefault)
		{
			if (UnrealVSPackage.Instance.OptionsPage.AllowUnrealVSCheckoutOnEdit)
			{
				DTE DTE = UnrealVSPackage.Instance.DTE;

				if (!DTE.Solution.Saved)
				{
					OpenForEdit(DTE.Solution.FullName);
				}

				foreach (Document OpenedFile in DTE.Documents)
				{
					if (OpenedFile.Saved == false)
					{
						OpenForEdit(OpenedFile.FullName);
					}
				}

				if (DTE.Solution.Projects == null)
				{
					return;
				}

				foreach (Project p in DTE.Solution.Projects)
				{
					EditProjectRecursive(p);
				}
			}
		}

		private void EditProjectRecursive(Project Proj)
		{
			if (!Proj.Saved)
			{
				OpenForEdit(Proj.FullName);
			}

			if (Proj.ProjectItems == null)
			{
				return;
			}

			foreach (ProjectItem ProjItem in Proj.ProjectItems)
			{
				if (ProjItem.SubProject != null)
				{
					EditProjectRecursive(ProjItem.SubProject);
				}
				else if (!ProjItem.Saved)
				{
					for (short Index = 1; Index <= ProjItem.FileCount; Index++)
					{
						OpenForEdit(ProjItem.get_FileNames(Index));
					}
				}
			}
		}

		private void UpdateMenuOptions()
		{
			DTE DTE = UnrealVSPackage.Instance.DTE;

			bool EnableCommands = UnrealVSPackage.Instance.OptionsPage.AllowUnrealVSP4;
			bool SolutionLoaded = IsSolutionLoaded();

			// update each menu item enabled command
			foreach (P4Command command in P4CommandsList)
			{
				command.Toggle(SolutionLoaded && EnableCommands);
			}

		}
		private void OnOptionsChanged(object Sender, EventArgs E)
		{
			UpdateMenuOptions();
		}

		public void Dispose()
		{
			KillChildProcess();
		}

		private void KillChildProcess()
		{
			if (ChildProcess != null)
			{
				if (!ChildProcess.HasExited)
				{
					ChildProcess.Kill();
					ChildProcess.WaitForExit();
				}
				ChildProcess.Dispose();
				ChildProcess = null;
			}
		}

		private void P4CheckoutButtonHandler(object Sender, EventArgs Args)
		{
			DTE DTE = UnrealVSPackage.Instance.DTE;

			// Check we've got a file open
			if (DTE.ActiveDocument == null)
			{
				Logging.WriteLine("P4Checkout called without an active document");

				P4OutputPane.Activate();
				P4OutputPane.OutputString($"1>------ P4Checkout called without an active document{Environment.NewLine}");

				return;
			}

			OpenForEdit(DTE.ActiveDocument.FullName);
		}
		private void P4AnnotateButtonHandler(object Sender, EventArgs Args)
		{
			DTE DTE = UnrealVSPackage.Instance.DTE;

			P4OutputPane.Activate();

			// Check we've got a file open
			if (DTE.ActiveDocument == null)
			{
				Logging.WriteLine("P4Annotate called without an active document");

				P4OutputPane.OutputString($"1>------ P4Annotate called without an active document{Environment.NewLine}");

				return;
			}

			// Call annotate itself
			bool Result = TryP4Command($"annotate -TcIqu \"{DTE.ActiveDocument.FullName}", PullWorkingDirectoryOn, OutputStdOutOff);

			if (!Result || P4OperationStdErr.Length > 0)
			{
				P4OutputPane.OutputString($"1>------ P4Annotate call failed");
				return;
			}

			// Extract the current document line number and the first line of text within it
			string FirstLine;
			int CurrentLine = 0;
			{
				TextDocument CurrentDocument = (TextDocument)(DTE.ActiveDocument.Object("TextDocument"));

				var EditPoint = CurrentDocument.StartPoint.CreateEditPoint();
				string DocumentContents = EditPoint.GetText(CurrentDocument.EndPoint);
				FirstLine = DocumentContents.Split('\n')[0];


				TextSelection TextSel = DTE.ActiveWindow.Selection as TextSelection;
				if (TextSel != null)
				{
					CurrentLine = TextSel.CurrentLine;
				}
			}

			// Pre-process the output to comment out the additions thus allowing
			// code to use correct syntax coloring - helps enormously with visualization
			StringBuilder EditedCopy = new StringBuilder();
			{
				// replace 
				//       13149436:            First.Last 2020/05/04 
				//
				// with
				// /*	13149436:            First.Last 2020/05/04*/

				// This is the per line offset of the annotation added to the document
				int AnnotateOffset = P4OperationStdOut.IndexOf(FirstLine);

				string[] AnnotateLines = P4OperationStdOut.Split('\n');

				foreach (string Line in AnnotateLines)
				{
					if (Line.Length > AnnotateOffset)
					{
						string EditedLine = Line.Insert(AnnotateOffset, "*/");
						EditedLine = EditedLine.Insert(0, "/*");

						EditedCopy.Append(EditedLine);
					}
					else
					{
						EditedCopy.Append(Line);
					}

				}
			}

			// Replace GetTempPath with the UBT intermediate folder if we have access
			string TempPath = Path.GetTempPath();
			string TempFileName = Path.GetFileNameWithoutExtension(DTE.ActiveDocument.FullName) + "_annotate" + Path.GetExtension(DTE.ActiveDocument.FullName);
			string TempFilePath = Path.Combine(TempPath, TempFileName);

			// Write out our temp file
			File.WriteAllText(TempFilePath, EditedCopy.ToString());

			// Open it, activate it and move to the line the user focused to execute the command
			DTE.ExecuteCommand("File.OpenFile", $"\"{TempFilePath}\"");
			DTE.ActiveDocument.Activate();

			TextSelection NewTextSel = DTE.ActiveWindow.Selection as TextSelection;
			if (NewTextSel != null)
			{
				NewTextSel.GotoLine(CurrentLine, false);
			}
		}
		private void P4ViewSelectedCLButtonHandler(object Sender, EventArgs Args)
		{
			DTE DTE = UnrealVSPackage.Instance.DTE;

			if (DTE.ActiveDocument == null)
			{
				return;
			}

			TextSelection TextSel = (TextSelection)DTE.ActiveDocument.Selection;

			int ChangeList = -1;

			try
			{
				ChangeList = Int32.Parse(TextSel.Text);
			}
			catch
			{
				ChangeList = -2;
			}


			if (ChangeList > 0)
			{
				TryP4VCCommand($"Change {ChangeList}");
			}

		}

		private void P4IntegrationAwareTimeLapseHandler(object Sender, EventArgs Args)
		{
			DTE DTE = UnrealVSPackage.Instance.DTE;

			if (DTE.ActiveDocument == null)
			{
				Logging.WriteLine("P4IntegrationAwareTimeLapse called without an active document");

				P4OutputPane.OutputString($"1>------ P4IntegrationAwareTimeLapse called without an active document{Environment.NewLine}");

				return;
			}

			string Command = $"-win 0 {UserInfoComplete} -cmd \"annotate -i \"{ DTE.ActiveDocument.FullName}\"\"";

			TryP4VCommand(Command);
		}

		private void ChangeDiffSetting()
		{
			if (UnrealVSPackage.Instance.OptionsPage.AllowUnrealVSOverrideDiffSettings)
			{
				bool Margin = true;
				UnrealVSPackage.Instance.EditorOptionsFactory.GlobalOptions.SetOptionValue("Diff/View/ShowDiffOverviewMargin", Margin);

				DifferenceHighlightMode HighlightMode = (DifferenceHighlightMode)3;
				UnrealVSPackage.Instance.EditorOptionsFactory.GlobalOptions.SetOptionValue("Diff/View/HighlightMode", HighlightMode);
			}
		}

		private void P4DiffinVSHandler(object Sender, EventArgs Args)
		{
			DTE DTE = UnrealVSPackage.Instance.DTE;

			if (DTE.ActiveDocument == null)
			{
				Logging.WriteLine("P4DiffInVSHandler called without an active document");

				P4OutputPane.OutputString($"1>------ P4DiffInVSHandler called without an active document{Environment.NewLine}");

				return;
			}

			ChangeDiffSetting();

			// get the HAVE revision
			// p4 fstat -T "haveRev" -Olp //UE5/Main/Engine/Source/Programs/UnrealVS/UnrealVS.Shared/P4Commands.cs
			if (TryP4Command($"fstat -T \"haveRev,depotFile\" -Olp \"{DTE.ActiveDocument.FullName}\""))
			{
				// expect output of the form
				//		"... haveRev 5"
				//		"... depotFile //UE5/Main/Engine/Source/Programs/UnrealVS/UnrealVS.Shared/P4Commands.cs
				Regex HavePattern = new Regex(@"... haveRev (?<Have>.*)$", RegexOptions.Compiled | RegexOptions.Multiline);
				Regex DepotPathPattern = new Regex(@"... depotFile (?<depotFile>.*)$", RegexOptions.Compiled | RegexOptions.Multiline);
				System.Text.RegularExpressions.Match HaveMatch = HavePattern.Match(P4OperationStdOut);
				System.Text.RegularExpressions.Match PathMatch = DepotPathPattern.Match(P4OperationStdOut);
				int HaveRev = Int32.Parse(HaveMatch.Groups["Have"].Value.Trim());
				string depotPath = PathMatch.Groups["depotFile"].Value.Trim();

				// Generate the Temp filename
				string TempPath = Path.GetTempPath();
				string TempFileName = Path.GetFileNameWithoutExtension(DTE.ActiveDocument.FullName) + "$" + HaveRev.ToString() + Path.GetExtension(DTE.ActiveDocument.FullName);
				string TempFilePath = Path.Combine(TempPath, TempFileName);

				// sync the HAVE revision to a file
				// p4 print //UE5/Main/Engine/Source/Programs/UnrealVS/UnrealVS.Shared/P4Commands.cs#5 >> file

				string VersionPath = $"{depotPath}#{HaveRev}";
				if (TryP4Command($"-q print \"{VersionPath}\""))
				{
					File.WriteAllText(TempFilePath, P4OperationStdOut);

					// Tools.DiffFiles SourceFile, TargetFile, [SourceDisplayName],[TargetDisplayName]
					DTE.ExecuteCommand("Tools.DiffFiles", $"\"{TempFilePath}\" \"{DTE.ActiveDocument.FullName}\" \"{VersionPath}\" \"{DTE.ActiveDocument.FullName}\"");
				}
			}

		}
		private void OpenForEdit(string FileName)
		{
			// Don't open for edit if the file is already writable
			if (!File.Exists(FileName) || !File.GetAttributes(FileName).HasFlag(FileAttributes.ReadOnly))
			{
				return;
			}

			TryP4Command($"edit \"{FileName}");
		}

		private string ReadWorkingDirectorFromP4Config()
		{
			string WorkingDirectory = "";
			//------P4 Operation started
			//P4CLIENT = andrew.firth_private_frosty2(config 'd:\p4\frosty\p4config.txt')
			//P4CONFIG = p4config.txt(set)(config 'd:\p4\frosty\p4config.txt')
			//P4EDITOR = C:\windows\SysWOW64\notepad.exe(set)
			//P4PORT = perforce:1666(config 'd:\p4\frosty\p4config.txt')
			//P4USER = andrew.firth(config 'd:\p4\frosty\p4config.txt')
			//P4_perforce:1666_CHARSET = none(set)

			TryP4Command("set", PullWorkingDirectoryOff);

			bool Success = false;

			if (P4OperationStdOut.Length > 0)
			{
				string[] lines = P4OperationStdOut.Split('\n');

				// very brittle - Index assume there is a better way leveraging C#
				if (lines.Length > 0)
				{
					if (lines[0].Contains("p4config.txt"))
					{
						string path = lines[0].Split('\'')[1];

						WorkingDirectory = Path.GetDirectoryName(path);
						Success = true;
					}
				}
			}

			if (!Success)
			{
				P4OutputPane.OutputString($"attempt to pull p4config.txt info failed{Environment.NewLine}");
			}

			return WorkingDirectory;
		}

		void SetUserInfoStrings()
		{
			TryP4Command($"-s -L \"{P4WorkingDirectory}\" info", PullWorkingDirectoryOff);

			Regex UserPattern = new Regex(@"User name: (?<user>.*)$", RegexOptions.Compiled | RegexOptions.Multiline);
			Regex PortPattern = new Regex(@"Server address: (?<port>.*)$", RegexOptions.Compiled | RegexOptions.Multiline);
			Regex ClientPattern = new Regex(@"Client name: (?<client>.*)$", RegexOptions.Compiled | RegexOptions.Multiline);

			System.Text.RegularExpressions.Match UserMatch = UserPattern.Match(P4OperationStdOut);
			System.Text.RegularExpressions.Match PortMatch = PortPattern.Match(P4OperationStdOut);
			System.Text.RegularExpressions.Match ClientMath = ClientPattern.Match(P4OperationStdOut);

			Port = PortMatch.Groups["port"].Value.Trim();
			Username = UserMatch.Groups["user"].Value.Trim();
			Client = ClientMath.Groups["client"].Value.Trim();

			UserInfoComplete = string.Format(" -p {0} -u {1} -c {2} ", Port, Username, Client);

			P4OutputPane.OutputString("GetUserInfoStringFull : " + UserInfoComplete);
		}

		private void PullWorkingDirectory(bool pullfromP4Settings)
		{
			if (IsSolutionLoaded() && (P4WorkingDirectory == null || P4WorkingDirectory.Length < 2))
			{
				DTE DTE = UnrealVSPackage.Instance.DTE;

				// use the current solution folder as a temp working directory
				P4WorkingDirectory = Path.GetDirectoryName(DTE.Solution.FileName);

				// SetUserInfoStrings 
				SetUserInfoStrings();
			}

			// if the callee wants us to pull a CWD and it wasn't already done
			// pull it from the p4 config now
			if (pullfromP4Settings && PullWorkingDirectorFromP4)
			{
				string NewWorkingDirectory = ReadWorkingDirectorFromP4Config();

				if (NewWorkingDirectory.Length > 1)
				{
					P4WorkingDirectory = NewWorkingDirectory;
					PullWorkingDirectorFromP4 = false;

					P4OutputPane.OutputString($"P4WorkingDirectory set to '{P4WorkingDirectory}'{Environment.NewLine}");
				}
				else
				{
					P4OutputPane.OutputString($"P4WorkingDirectory set failed {Environment.NewLine}");
				}


			}
		}

		private bool TryP4Command(string CommandLine, bool PullWorkingDirectoryNow = PullWorkingDirectoryOn, bool OutputStdOut = OutputStdOutOn)
		{
			return TryP4CommandEx(P4Exe, CommandLine, PullWorkingDirectoryNow, OutputStdOut);
		}

		private bool TryP4VCCommand(string CommandLine, bool PullWorkingDirectoryNow = PullWorkingDirectoryOn, bool OutputStdOut = OutputStdOutOn)
		{
			if (P4VCCmd.Length > 1)
			{
				return TryP4CommandEx(P4VCCmd, CommandLine, PullWorkingDirectoryNow, OutputStdOut);
			}

			return false;
		}

		private bool TryP4VCommand(string CommandLine, bool PullWorkingDirectoryNow = PullWorkingDirectoryOn, bool OutputStdOut = OutputStdOutOn)
		{
			return TryP4CommandEx(P4VExe, CommandLine, PullWorkingDirectoryNow, OutputStdOut);
		}

		private bool TryP4CommandEx(string CmdPath, string CommandLine, bool PullWorkingDirectoryNow = PullWorkingDirectoryOn, bool OutputStdOut = OutputStdOutOn)
		{
			PullWorkingDirectory(PullWorkingDirectoryNow);

			DTE DTE = UnrealVSPackage.Instance.DTE;

			// If there's already a operation in progress, abort... potential issues here
			// in the OnSave/OnSaveAll operations.
			if (ChildProcess != null && !ChildProcess.HasExited)
			{
				P4OutputPane.OutputString($"P4 operation already in flight.{Environment.NewLine}");
				P4OutputPane.Activate();
				return false;
			}

			// Make sure any existing op is stopped
			KillChildProcess();

			// Set up the output pane
			P4OutputPane.Activate();
			P4OutputPane.OutputString($"1>------ P4 Operation started{Environment.NewLine}");
			P4OutputPane.OutputString($"CWD: {P4WorkingDirectory}{Environment.NewLine}");
			P4OutputPane.OutputString($"CMD: {P4Exe} {CommandLine}{Environment.NewLine}");

			// Create a delegate for handling output messages
			//void OutputHandler(object Sender, DataReceivedEventArgs Args) { if (Args.Data != null) P4OperationStdOut += $"{Args.Data}{Environment.NewLine}"; }
			//void ErrOutputHandler(object Sender, DataReceivedEventArgs Args) { if (Args.Data != null) P4OperationStdErr += $"{Args.Data}{Environment.NewLine}"; }

			StringBuilder StdOutSB = new StringBuilder();
			StringBuilder StdErrSB = new StringBuilder();

			// Spawn the new process
			ChildProcess = new System.Diagnostics.Process()
			{
				StartInfo = new ProcessStartInfo()
				{
					FileName = CmdPath,
					Arguments = CommandLine,
					WorkingDirectory = P4WorkingDirectory,
					UseShellExecute = false,
					RedirectStandardOutput = true,
					RedirectStandardError = true,
					CreateNoWindow = true
				}
			};
			// Create a delegate for handling output messages
			ChildProcess.OutputDataReceived += (s, a) => { if (a.Data != null) StdOutSB.AppendLine(a.Data); };
			ChildProcess.ErrorDataReceived += (s, a) => { if (a.Data != null) StdErrSB.AppendLine(a.Data); };
			ChildProcess.Start();
			ChildProcess.BeginOutputReadLine();
			ChildProcess.BeginErrorReadLine();

			ChildProcess.WaitForExit();

			P4OperationStdOut = StdOutSB.ToString();
			P4OperationStdErr = StdErrSB.ToString();

			if (P4OperationStdOut.Length > 0 && OutputStdOut)
			{
				P4OutputPane.OutputString(P4OperationStdOut);
			}

			if (P4OperationStdErr.Length > 0)
			{
				P4OutputPane.OutputString(P4OperationStdErr);
			}

			P4OutputPane.OutputString($"1>------ P4 Operation complete{Environment.NewLine}");

			return P4OperationStdErr.Length == 0;
		}
	}
}
