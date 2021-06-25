// Copyright Epic Games, Inc. All Rights Reserved.

using EnvDTE;
using EnvDTE80;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.OLE.Interop;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;
using System;
using System.Collections.Generic;
using System.ComponentModel.Design;
using System.Diagnostics;
using System.IO;
using System.Windows.Forms;
using Microsoft.Win32;

namespace UnrealVS
{
	internal class P4Commands : IDisposable
	{
		private const bool PullWorkingDirectoryOn = true;
		private const bool PullWorkingDirectoryOff = false;

		private const bool OutputStdOutOn= true;
		private const bool OutputStdOutOff = false;

		private const int P4SubMenuID = 0x3100;
		private const int P4CheckoutButtonID = 0x1450;
		private const int P4AnnotateButtonID = 0x1451;

		private OleMenuCommand SubMenuCommand;

		private System.Diagnostics.Process ChildProcess;
		private IVsOutputWindowPane P4OutputPane;
		private string P4WorkingDirectory;
		private bool PullWorkignDirectoryFromP4 = true;

		// stdXX from last operation
		private string P4OperationStdOut;
		private string P4OperationStdErr;

		// potentially pull this from registry - assume it exists there.
		private string P4Exe = "C:\\Program Files\\Perforce\\p4.exe";

		private List<CommandEvents> EventsForce = new List<CommandEvents>();

		private class P4Command
		{
			public int ButtonID;

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
			UnrealVSPackage.Instance.OnSolutionOpened += UpdateP4SoltuionBinding;
			UnrealVSPackage.Instance.OnSolutionClosed += UpdateP4SoltuionBinding;

			// create specific output window for unrealvs.P4
			P4OutputPane = UnrealVSPackage.Instance.GetP4OutputPane();

			// add commands
			P4CommandsList.Add(new P4Command(P4CheckoutButtonID, P4CheckoutButtonHandler));
			P4CommandsList.Add(new P4Command(P4AnnotateButtonID, P4AnnotateButtonHandler));

			// add sub menu for commands
			SubMenuCommand = new OleMenuCommand(null, new CommandID(GuidList.UnrealVSCmdSet, P4SubMenuID));
			SubMenuCommand.BeforeQueryStatus += OnQuickBuildSubMenuQuery;
			UnrealVSPackage.Instance.MenuCommandService.AddCommand(SubMenuCommand);

			// Update the menu visibility to enforce user options.
			UpdateMenuOptions();
		}
		// Called when solutions are loaded or unloaded
		private void UpdateP4SoltuionBinding()
		{
			// Update the menu visibility
			UpdateMenuOptions();

			DTE DTE = UnrealVSPackage.Instance.DTE;

			if (IsSolutionLoaded())
			{
				// use the current solution folder as a temp working directory
				P4WorkingDirectory = Path.GetDirectoryName(DTE.Solution.FileName);

				// force the system to pull the real working directory from p4
				PullWorkignDirectoryFromP4 = true;
			}
			else
			{
				// reset the working directory to avoid sending commands to old streams
				P4WorkingDirectory = "";
			}
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

			// hook up the on "Save" list - internally they verify the users choice
			RegisterCallbackHandler("File.SaveSelectedItems", SaveSelectedCallback);
			RegisterCallbackHandler("File.SaveAll", OnSaveAll);
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
			bool Result= TryP4Command($"annotate -TcIqu \"{DTE.ActiveDocument.FullName}", PullWorkingDirectoryOn, OutputStdOutOff);

			if (!Result || P4OperationStdErr.Length>0)
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
			string EditedCopyOfAnnotate = "";
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

						EditedCopyOfAnnotate += EditedLine;
					}
					else
					{
						EditedCopyOfAnnotate += Line;
					}

				}
			}

			// Replace GetTempPath with the UBT intermediate folder if we have access
			string TempPath = Path.GetTempPath();
			string TempFileName = Path.GetFileNameWithoutExtension(DTE.ActiveDocument.FullName) + "_annotate" + Path.GetExtension(DTE.ActiveDocument.FullName);
			string TempFilePath = Path.Combine(TempPath, TempFileName);

			// Write out our temp file
			File.WriteAllText(TempFilePath, EditedCopyOfAnnotate);

			// Open it, activate it and move to the line the user focused to execute the command
			DTE.ExecuteCommand("File.OpenFile", $"\"{TempFilePath}\"");
			DTE.ActiveDocument.Activate();

			TextSelection NewTextSel = DTE.ActiveWindow.Selection as TextSelection;
			if (NewTextSel != null)
			{
				NewTextSel.GotoLine(CurrentLine, false);
			}
		}

		private void OpenForEdit(string FileName)
		{
			TryP4Command($"edit \"{FileName}");
		}

		private void ReadP4UserInfo()
		{
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

						P4WorkingDirectory = Path.GetDirectoryName(path);
						P4OutputPane.OutputString($"P4WorkingDirectory set to {P4WorkingDirectory}{Environment.NewLine}");
						Success = true;
					}
				}
			}

			if (!Success)
			{
				P4OutputPane.OutputString($"attempt to full p4config.txt info failed, P4WorkingDirectory set to {P4WorkingDirectory}{Environment.NewLine}");
			}
		}

		private void PullWorkingDirectory()
		{
			if (PullWorkignDirectoryFromP4)
			{
				ReadP4UserInfo();
				PullWorkignDirectoryFromP4 = false;
			}
		}

		private bool TryP4Command(string CommandLine, bool PullWorkingDirectoryNow = PullWorkingDirectoryOn, bool OutputStdOut = OutputStdOutOn)
		{
			if (PullWorkingDirectoryNow)
			{
				PullWorkingDirectory();
			}
		
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
			P4OutputPane.OutputString($"CMD: {P4Exe} {CommandLine}{Environment.NewLine}");

			// Create a delegate for handling output messages
			void OutputHandler(object Sender, DataReceivedEventArgs Args) { if (Args.Data != null) P4OperationStdOut += $"{Args.Data}{Environment.NewLine}"; }
			void ErrOutputHandler(object Sender, DataReceivedEventArgs Args) { if (Args.Data != null) P4OperationStdErr += $"{Args.Data}{Environment.NewLine}"; }

			P4OperationStdOut = "";
			P4OperationStdErr = "";

			// Spawn the new process
			ChildProcess = new System.Diagnostics.Process();
			ChildProcess.StartInfo.FileName = P4Exe;
			ChildProcess.StartInfo.Arguments = CommandLine;
			ChildProcess.StartInfo.WorkingDirectory = P4WorkingDirectory;
			ChildProcess.StartInfo.UseShellExecute = false;
			ChildProcess.StartInfo.RedirectStandardOutput = true;
			ChildProcess.StartInfo.RedirectStandardError = true;
			ChildProcess.StartInfo.CreateNoWindow = true;
			ChildProcess.OutputDataReceived += OutputHandler;
			ChildProcess.ErrorDataReceived += ErrOutputHandler;
			ChildProcess.Start();
			ChildProcess.BeginOutputReadLine();
			ChildProcess.BeginErrorReadLine();

			ChildProcess.WaitForExit();

			if (P4OperationStdOut.Length > 0 && OutputStdOut)
			{
				P4OutputPane.OutputString(P4OperationStdOut);
			}

			if (P4OperationStdErr.Length > 0)
			{
				P4OutputPane.OutputString(P4OperationStdErr);
			}

			P4OutputPane.OutputString($"1>------ P4 Operation complete{Environment.NewLine}");

			return true;
		}
	}
}
