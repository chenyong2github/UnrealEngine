// Copyright Epic Games, Inc. All Rights Reserved.

using EnvDTE;
using Microsoft.VisualStudio;
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
using System.Threading;
using System.Threading.Tasks;

namespace UnrealVS
{
	internal class P4Commands : IDisposable
	{
		private const bool bPullWorkingDirectoryOn = true;
		private const bool bPullWorkingDirectoryOff = false;

		private const bool bOutputStdOutOn = true;
		private const bool bOutputStdOutOff = false;

		private const bool bWaitForCompletionOn = true;
		private const bool bWaitForCompletionOff = false;

		private const int P4SubMenuID = 0x3100;
		private const int P4CheckoutButtonID = 0x1450;
		private const int P4AnnotateButtonID = 0x1451;
		private const int P4ViewSelectedCLButtonID = 0x1452;
		private const int P4IntegrationAwareTimelapseButtonID = 0x1453;
		private const int P4DiffinVSButtonID = 0x1454;
		private const int P4GetLast10ChangesID = 0x1455;
		private const int P4ShowFileinP4VID = 0x1456;

		private OleMenuCommand SubMenuCommand;

		//private System.Diagnostics.Process ChildProcess;

		private List<System.Diagnostics.Process> ChildProcessList = new List<System.Diagnostics.Process>();

		private IVsOutputWindowPane P4OutputPane;
		private string P4WorkingDirectory;
		private bool bPullWorkingDirectorFromP4 = true;
		private static Mutex bPullWorkingDirectorFromP4Mutex = new Mutex();

		// stdXX from last operation
		//private string P4OperationStdOut;
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

		private IntercepteSave Interceptor;

		private bool IsSolutionLoaded()
		{
			ThreadHelper.ThrowIfNotOnUIThread();

			DTE DTE = UnrealVSPackage.Instance.DTE;

			return DTE.Solution.FileName.Length > 0;
		}

		private void OnQuickBuildSubMenuQuery(object sender, EventArgs e)
		{
			ThreadHelper.ThrowIfNotOnUIThread();

			DTE DTE = UnrealVSPackage.Instance.DTE;

			bool bEnableCommands = UnrealVSPackage.Instance.OptionsPage.AllowUnrealVSP4;
			bool bSolutionLoaded = IsSolutionLoaded();

			var SenderSubMenuCommand = (OleMenuCommand)sender;

			SenderSubMenuCommand.Visible = SenderSubMenuCommand.Enabled = bEnableCommands & bSolutionLoaded;
		}

		public P4Commands()
		{
			ThreadHelper.ThrowIfNotOnUIThread();

			// setup callbacks on IDE operations
			UnrealVSPackage.Instance.OptionsPage.OnOptionsChanged += OnOptionsChanged;
			UnrealVSPackage.Instance.OnSolutionOpened += SolutionOpened;
			UnrealVSPackage.Instance.OnSolutionClosed += SolutionClosed;

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
			P4CommandsList.Add(new P4Command(P4GetLast10ChangesID, P4GetLast10ChangesHandler));
			P4CommandsList.Add(new P4Command(P4ShowFileinP4VID, P4ShowFileInP4Vandler));

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

			var runningDocumentTable = new RunningDocumentTable(UnrealVSPackage.Instance);
			Interceptor = new IntercepteSave(UnrealVSPackage.Instance.DTE, runningDocumentTable, this);

			runningDocumentTable.Advise(Interceptor);

		}
		// Called when solutions are loaded or unloaded
		private void SolutionOpened()
		{
			// Update the menu visibility
			UpdateMenuOptions();
		}

		private void SolutionClosed()
		{
			ThreadHelper.ThrowIfNotOnUIThread();

			// Update the menu visibility
			UpdateMenuOptions();

			// Clear any existing P4 working directory settings
			P4WorkingDirectory = "";
			bPullWorkingDirectorFromP4 = true;
		}

		void RegisterCallbackHandler(string CommandName, _dispCommandEvents_BeforeExecuteEventHandler Callback)
		{
			ThreadHelper.ThrowIfNotOnUIThread();

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

		private void UpdateMenuOptions()
		{
			ThreadHelper.ThrowIfNotOnUIThread();

			DTE DTE = UnrealVSPackage.Instance.DTE;

			bool bEnableCommands = UnrealVSPackage.Instance.OptionsPage.AllowUnrealVSP4;
			bool bSolutionLoaded = IsSolutionLoaded();

			// update each menu item enabled command
			foreach (P4Command command in P4CommandsList)
			{
				command.Toggle(bSolutionLoaded && bEnableCommands);
			}

		}
		private void OnOptionsChanged(object Sender, EventArgs E)
		{
			ThreadHelper.ThrowIfNotOnUIThread();
			UpdateMenuOptions();
		}

		public void Dispose()
		{
			KillChildProcess();
		}

		private void KillChildProcess()
		{
			foreach(var OldProcess in ChildProcessList)
			{
				if (!OldProcess.HasExited)
				{
					OldProcess.Kill();
					OldProcess.WaitForExit();
				}
				OldProcess.Dispose();
			}
		}

		private void P4CheckoutButtonHandler(object Sender, EventArgs Args)
		{
			ThreadHelper.ThrowIfNotOnUIThread();

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
			ThreadHelper.ThrowIfNotOnUIThread();

			// Debug.ListCallStack

			DTE DTE = UnrealVSPackage.Instance.DTE;

			P4OutputPane.Activate();

			// Check we've got a file open
			if (DTE.ActiveDocument == null)
			{
				Logging.WriteLine("P4Annotate called without an active document");

				P4OutputPane.OutputString($"1>------ P4Annotate called without an active document{Environment.NewLine}");

				return;
			}

			string CaptureP4Output = "";

			// Call annotate itself
			bool bResult = TryP4Command($"annotate -TcIqu \"{DTE.ActiveDocument.FullName}", out CaptureP4Output, bPullWorkingDirectoryOn, bOutputStdOutOff);

			if (!bResult || P4OperationStdErr.Length > 0)
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
				int AnnotateOffset = CaptureP4Output.IndexOf(FirstLine);

				string[] AnnotateLines = CaptureP4Output.Split('\n');

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
			ThreadHelper.ThrowIfNotOnUIThread();

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
			ThreadHelper.ThrowIfNotOnUIThread();

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
				bool bMargin = true;
				UnrealVSPackage.Instance.EditorOptionsFactory.GlobalOptions.SetOptionValue("Diff/View/ShowDiffOverviewMargin", bMargin);

				DifferenceHighlightMode HighlightMode = (DifferenceHighlightMode)3;
				UnrealVSPackage.Instance.EditorOptionsFactory.GlobalOptions.SetOptionValue("Diff/View/HighlightMode", HighlightMode);
			}
		}

		private void P4DiffinVSHandler(object Sender, EventArgs Args)
		{
			ThreadHelper.ThrowIfNotOnUIThread();

			DTE DTE = UnrealVSPackage.Instance.DTE;

			if (DTE.ActiveDocument == null)
			{
				Logging.WriteLine("P4DiffInVSHandler called without an active document");

				P4OutputPane.OutputString($"1>------ P4DiffInVSHandler called without an active document{Environment.NewLine}");

				return;
			}

			ChangeDiffSetting();

			string CaptureP4Output = "";

			// get the HAVE revision
			// p4 fstat -T "haveRev" -Olp //UE5/Main/Engine/Source/Programs/UnrealVS/UnrealVS.Shared/P4Commands.cs
			if (TryP4Command($"fstat -T \"haveRev,depotFile\" -Olp \"{DTE.ActiveDocument.FullName}\"", out CaptureP4Output))
			{
				// expect output of the form
				//		"... haveRev 5"
				//		"... depotFile //UE5/Main/Engine/Source/Programs/UnrealVS/UnrealVS.Shared/P4Commands.cs
				Regex HavePattern = new Regex(@"... haveRev (?<Have>.*)$", RegexOptions.Compiled | RegexOptions.Multiline);
				Regex DepotPathPattern = new Regex(@"... depotFile (?<depotFile>.*)$", RegexOptions.Compiled | RegexOptions.Multiline);
				System.Text.RegularExpressions.Match HaveMatch = HavePattern.Match(CaptureP4Output);
				System.Text.RegularExpressions.Match PathMatch = DepotPathPattern.Match(CaptureP4Output);
				int HaveRev = Int32.Parse(HaveMatch.Groups["Have"].Value.Trim());
				string depotPath = PathMatch.Groups["depotFile"].Value.Trim();

				// Generate the Temp filename
				string TempPath = Path.GetTempPath();
				string TempFileName = Path.GetFileNameWithoutExtension(DTE.ActiveDocument.FullName) + "$" + HaveRev.ToString() + Path.GetExtension(DTE.ActiveDocument.FullName);
				string TempFilePath = Path.Combine(TempPath, TempFileName);

				// sync the HAVE revision to a file
				// p4 print //UE5/Main/Engine/Source/Programs/UnrealVS/UnrealVS.Shared/P4Commands.cs#5 >> file

				string CaptureP4Output2 = "";

				string VersionPath = $"{depotPath}#{HaveRev}";
				if (TryP4Command($"-q print \"{VersionPath}\"",out CaptureP4Output2, bPullWorkingDirectoryOn, bOutputStdOutOff))
				{
					File.WriteAllText(TempFilePath, CaptureP4Output2);

					// Tools.DiffFiles SourceFile, TargetFile, [SourceDisplayName],[TargetDisplayName]
					DTE.ExecuteCommand("Tools.DiffFiles", $"\"{TempFilePath}\" \"{DTE.ActiveDocument.FullName}\" \"{VersionPath}\" \"{DTE.ActiveDocument.FullName}\"");
				}
			}
		}

		private void P4GetLast10ChangesHandler(object Sender, EventArgs Args)
		{
			ThreadHelper.ThrowIfNotOnUIThread();

			DTE DTE = UnrealVSPackage.Instance.DTE;

			string TempPath = Path.GetTempPath();

			// generate the changes list
			string ChangesTempFileName = Path.GetFileNameWithoutExtension(DTE.ActiveDocument.FullName) + "_last_10_changelists" + Path.GetExtension(DTE.ActiveDocument.FullName);
			string ChangesTempFilePath = Path.Combine(TempPath, ChangesTempFileName);

			string CaptureP4Output = "";
			if (TryP4Command($"changes -itls submitted -m 10 {DTE.ActiveDocument.FullName}", out CaptureP4Output, bOutputStdOutOff))
			{
				File.WriteAllText(ChangesTempFilePath, CaptureP4Output);

				DTE.ExecuteCommand("File.OpenFile", $"\"{ChangesTempFilePath}\"");
				DTE.ActiveDocument.Activate();
				DTE.ExecuteCommand("Window.NewVerticalTabGroup");
			}
		}

		private void P4ShowFileInP4Vandler(object Sender, EventArgs Args)
		{
			ThreadHelper.ThrowIfNotOnUIThread();

			DTE DTE = UnrealVSPackage.Instance.DTE;

			if (DTE.ActiveDocument == null)
			{
				return;
			}

			TryP4VCommand($"-s {DTE.ActiveDocument.FullName}");
		}
		public void OpenForEdit(string FileName, bool CheckOptionsFlag = false)
		{
			// if the callee requested it, honor the enabling of autocheckout option - return if its off
			if (CheckOptionsFlag && !UnrealVSPackage.Instance.OptionsPage.AllowUnrealVSCheckoutOnEdit)
			{
				//P4OutputPane.OutputString($"autocheckout disabled: {FileName}");
				return;
			}	

			// Don't open for edit if the file is already writable
			if (!File.Exists(FileName) || !File.GetAttributes(FileName).HasFlag(FileAttributes.ReadOnly))
			{
				//P4OutputPane.OutputString($"already writeable: {FileName}");
				return;
			}

			if (UnrealVSPackage.Instance.OptionsPage.AllowAsyncP4Checkout)
			{
				//P4OutputPane.OutputString($"Marking file as writeable: {FileName}");
				// Mark the file as writeable
				FileAttributes NewAttributes = File.GetAttributes(FileName) & ~FileAttributes.ReadOnly;
				File.SetAttributes(FileName, NewAttributes);

				// then send a fire and forget p4 edit command - does not lock the UI
				TryP4CommandAsync($"edit \"{FileName}");
			}
			else
			{
				string CaptureP4Output = "";
				// sync edit command - will lock the UI but be safer (no risk of a locally writeable file that is not checked out)
				TryP4Command($"edit \"{FileName}", out CaptureP4Output);
			}
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

			string CaptureP4Output = "";
			TryP4Command("set", out CaptureP4Output, bPullWorkingDirectoryOff);

			bool bSuccess = false;

			if (CaptureP4Output.Length > 0)
			{
				string[] lines = CaptureP4Output.Split('\n');

				// very brittle - Index assume there is a better way leveraging C#
				if (lines.Length > 0)
				{
					if (lines[0].Contains("p4config.txt"))
					{
						string path = lines[0].Split('\'')[1];

						WorkingDirectory = Path.GetDirectoryName(path);
						bSuccess = true;
					}
				}
			}

			if (!bSuccess)
			{
				P4OutputPane.OutputString($"attempt to pull p4config.txt info failed{Environment.NewLine}");
			}

			return WorkingDirectory;
		}

		void SetUserInfoStrings()
		{
			string CaptureP4Output = "";
			TryP4Command($"-s -L \"{P4WorkingDirectory}\" info", out CaptureP4Output, bPullWorkingDirectoryOff);

			Regex UserPattern = new Regex(@"User name: (?<user>.*)$", RegexOptions.Compiled | RegexOptions.Multiline);
			Regex PortPattern = new Regex(@"Server address: (?<port>.*)$", RegexOptions.Compiled | RegexOptions.Multiline);
			Regex ClientPattern = new Regex(@"Client name: (?<client>.*)$", RegexOptions.Compiled | RegexOptions.Multiline);

			System.Text.RegularExpressions.Match UserMatch = UserPattern.Match(CaptureP4Output);
			System.Text.RegularExpressions.Match PortMatch = PortPattern.Match(CaptureP4Output);
			System.Text.RegularExpressions.Match ClientMath = ClientPattern.Match(CaptureP4Output);

			Port = PortMatch.Groups["port"].Value.Trim();
			Username = UserMatch.Groups["user"].Value.Trim();
			Client = ClientMath.Groups["client"].Value.Trim();

			UserInfoComplete = string.Format(" -p {0} -u {1} -c {2} ", Port, Username, Client);

			P4OutputPane.OutputString("GetUserInfoStringFull : " + UserInfoComplete + Environment.NewLine);
		}

		private void PullWorkingDirectory(bool bPullfromP4Settings)
		{
			ThreadHelper.ThrowIfNotOnUIThread();

			bPullWorkingDirectorFromP4Mutex.WaitOne();

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
			if (bPullfromP4Settings && bPullWorkingDirectorFromP4)
			{
				string NewWorkingDirectory = ReadWorkingDirectorFromP4Config();

				if (NewWorkingDirectory.Length > 1)
				{
					P4WorkingDirectory = NewWorkingDirectory;
					bPullWorkingDirectorFromP4 = false;

					P4OutputPane.OutputString($"P4WorkingDirectory set to '{P4WorkingDirectory}'{Environment.NewLine}");
				}
				else
				{
					P4OutputPane.OutputString($"P4WorkingDirectory set failed {Environment.NewLine}");
				}
			}

			bPullWorkingDirectorFromP4Mutex.ReleaseMutex();
		}

		public async Task<bool> TryP4CommandAsync(string CommandLine, bool bPullWorkingDirectoryNow = bPullWorkingDirectoryOn, bool bOutputStdOut = bOutputStdOutOn)
		{
			string CaptureP4Output;
			return await System.Threading.Tasks.Task.Run( () => TryP4Command(CommandLine, out CaptureP4Output, bPullWorkingDirectoryNow, bOutputStdOut) );
		}

		private bool TryP4Command(string CommandLine, out string CaptureP4Output, bool bPullWorkingDirectoryNow = bPullWorkingDirectoryOn, bool bOutputStdOut = bOutputStdOutOn)
		{
			return TryP4CommandEx(P4Exe, CommandLine, out CaptureP4Output, bPullWorkingDirectoryNow, bOutputStdOut);
		}

		private bool TryP4VCCommand(string CommandLine, bool bPullWorkingDirectoryNow = bPullWorkingDirectoryOn, bool bOutputStdOut = bOutputStdOutOn)
		{
			if (P4VCCmd.Length > 1)
			{
				string ignore;
				return TryP4CommandEx(P4VCCmd, CommandLine, out ignore, bPullWorkingDirectoryNow, bOutputStdOut);
			}

			return false;
		}

		private bool TryP4VCommand(string CommandLine, bool bPullWorkingDirectoryNow = bPullWorkingDirectoryOn, bool bOutputStdOut = bOutputStdOutOn)
		{
			string ignore;
			return TryP4CommandEx(P4VExe, CommandLine, out ignore, bPullWorkingDirectoryNow, bOutputStdOut, bWaitForCompletionOff);
		}

		private bool TryP4CommandEx(string CmdPath, string CommandLine, out string CaptureP4Output, bool bPullWorkingDirectoryNow = bPullWorkingDirectoryOn, bool bOutputStdOut = bOutputStdOutOn, bool bWaitForCompletion = bWaitForCompletionOn)
		{
			PullWorkingDirectory(bPullWorkingDirectoryNow);

			DTE DTE = UnrealVSPackage.Instance.DTE;

			// If there's already a operation in progress, abort... potential issues here
			// in the OnSave/OnSaveAll operations where we will stall.
/*			if (ChildProcess != null && !ChildProcess.HasExited)
			{
				P4OutputPane.OutputString($"P4 operation already in flight...waiting{Environment.NewLine}");
				P4OutputPane.Activate();
				ChildProcess.WaitForExit();
			}*/

			// Make sure any existing op is stopped
			//KillChildProcess();

			// Set up the output pane
			P4OutputPane.Activate();
			//P4OutputPane.OutputString($"1>------ P4 Operation started{Environment.NewLine}");
			//P4OutputPane.OutputString($"CWD: {P4WorkingDirectory}{Environment.NewLine}");
			//P4OutputPane.OutputString($"CMD: {P4Exe} {CommandLine}{Environment.NewLine}");

			StringBuilder StdOutSB = new StringBuilder();
			StringBuilder StdErrSB = new StringBuilder();

			// Spawn the new process
			System.Diagnostics.Process ChildProcess = new System.Diagnostics.Process()
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
			if (bWaitForCompletion == false)
			{
				ChildProcess.EnableRaisingEvents = true;
				ChildProcess.Exited += (s, a) =>
				{
					string P4Output = StdOutSB.ToString();
					P4OperationStdErr = StdErrSB.ToString();

					if (P4Output.Length > 0 && bOutputStdOut)
					{
						P4OutputPane.OutputString(P4Output);
					}

					if (P4OperationStdErr.Length > 0)
					{
						P4OutputPane.OutputString(P4OperationStdErr);
					}

					//P4OutputPane.OutputString($"1>------ P4 Operation complete{Environment.NewLine}");

					ChildProcessList.Remove(ChildProcess);
				};
			}

			ChildProcessList.Add(ChildProcess);

			ChildProcess.Start();
			ChildProcess.BeginOutputReadLine();
			ChildProcess.BeginErrorReadLine();

			if (bWaitForCompletion)
			{ 
				ChildProcess.WaitForExit();

				CaptureP4Output = StdOutSB.ToString();
				P4OperationStdErr = StdErrSB.ToString();

				if (CaptureP4Output.Length > 0 && bOutputStdOut)
				{
					P4OutputPane.OutputString(CaptureP4Output);
				}

				if (P4OperationStdErr.Length > 0)
				{
					P4OutputPane.OutputString(P4OperationStdErr);
				}

				//P4OutputPane.OutputString($"1>------ P4 Operation complete{Environment.NewLine}");

				ChildProcessList.Remove(ChildProcess);

				return P4OperationStdErr.Length == 0;
			}
			else
			{
				CaptureP4Output = "async operations do not capture p4 output";
				return true;
			}
		}
	}

	internal class IntercepteSave : IVsRunningDocTableEvents3
	{
		private readonly DTE DTE;
		private readonly RunningDocumentTable RunningDocumentTable;
		private readonly P4Commands P4Ops;

		public IntercepteSave(DTE InDTE, RunningDocumentTable InRrunningDocumentTable, P4Commands InP4Ops)
		{
			DTE = InDTE;
			RunningDocumentTable = InRrunningDocumentTable;
			P4Ops = InP4Ops;
		}

		public int OnBeforeSave(uint DocumentCookie)
		{
			RunningDocumentInfo DocumentInfo = RunningDocumentTable.GetDocumentInfo(DocumentCookie);

			string AbsoluteFilePath = DocumentInfo.Moniker;

			P4Ops.OpenForEdit(AbsoluteFilePath, true);

			return VSConstants.S_OK;
		}

		// we are only using OnBeforeSave - but the interface requires us to define the whole interface.
		public int OnAfterFirstDocumentLock(uint docCookie, uint dwRdtLockType, uint dwReadLocksRemaining, uint dwEditLocksRemaining)
		{
			return VSConstants.S_OK;
		}

		public int OnBeforeLastDocumentUnlock(uint docCookie, uint dwRdtLockType, uint dwReadLocksRemaining, uint dwEditLocksRemaining)
		{
			return VSConstants.S_OK;
		}

		public int OnAfterSave(uint docCookie)
		{
			return VSConstants.S_OK;
		}

		public int OnAfterAttributeChange(uint docCookie, uint grfAttribs)
		{
			return VSConstants.S_OK;
		}

		public int OnBeforeDocumentWindowShow(uint docCookie, int fFirstShow, IVsWindowFrame pFrame)
		{
			return VSConstants.S_OK;
		}

		public int OnAfterDocumentWindowHide(uint docCookie, IVsWindowFrame pFrame)
		{
			return VSConstants.S_OK;
		}

		int IVsRunningDocTableEvents3.OnAfterAttributeChangeEx(uint docCookie, uint grfAttribs, IVsHierarchy pHierOld, uint itemidOld,
			string pszMkDocumentOld, IVsHierarchy pHierNew, uint itemidNew, string pszMkDocumentNew)
		{
			return VSConstants.S_OK;
		}

		int IVsRunningDocTableEvents2.OnAfterAttributeChangeEx(uint docCookie, uint grfAttribs, IVsHierarchy pHierOld, uint itemidOld,
			string pszMkDocumentOld, IVsHierarchy pHierNew, uint itemidNew, string pszMkDocumentNew)
		{
			return VSConstants.S_OK;
		}
	}
}
