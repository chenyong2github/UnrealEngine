// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using System.Windows.Forms;

using Autodesk.Revit.Attributes;
using Autodesk.Revit.DB;
using Autodesk.Revit.UI;

namespace DatasmithRevitExporter
{
	public abstract class DatasmithRevitCommand : IExternalCommand
	{
		public const string DIALOG_CAPTION = "Export 3D View to Unreal Datasmith";

		public Result Execute(
			ExternalCommandData InCommandData,		// contains reference to Application and View
			ref string			OutCommandMessage,  // error message to display in the failure dialog when the command returns "Failed"
			ElementSet			OutElements         // set of problem elements to display in the failure dialog when the command returns "Failed"
		)
		{
			Autodesk.Revit.ApplicationServices.Application Application = InCommandData.Application.Application;

			if (string.Compare(Application.VersionNumber, "2018", StringComparison.Ordinal) == 0 && string.Compare(Application.SubVersionNumber, "2018.3", StringComparison.Ordinal) < 0)
			{
				string Message = string.Format("The running Revit is not supported.\nYou must use Revit 2018.3 or further updates to export.");
				MessageBox.Show(Message, DIALOG_CAPTION, MessageBoxButtons.OK, MessageBoxIcon.Warning);
				return Result.Cancelled;
			}

			if (!CustomExporter.IsRenderingSupported())
			{
				string Message = "3D view rendering is not supported in the running Revit.";
				MessageBox.Show(Message, DIALOG_CAPTION, MessageBoxButtons.OK, MessageBoxIcon.Warning);
				return Result.Cancelled;
			}

			UIDocument UIDoc = InCommandData.Application.ActiveUIDocument;

			if (UIDoc == null)
			{
				string Message = "You must be in a document to export.";
				MessageBox.Show(Message, DIALOG_CAPTION, MessageBoxButtons.OK, MessageBoxIcon.Warning);
				return Result.Cancelled;
			}

			return OnExecute(InCommandData, ref OutCommandMessage, OutElements);
		}

		public abstract Result OnExecute(ExternalCommandData InCommandData, ref string OutCommandMessage, ElementSet OutElements);
	}

	[Transaction(TransactionMode.Manual)]
	public class DatasmithSyncRevitCommand : DatasmithRevitCommand
	{
		public override Result OnExecute(ExternalCommandData InCommandData, ref string OutCommandMessage, ElementSet OutElements)
		{
			UIDocument UIDoc = InCommandData.Application.ActiveUIDocument;
			Document Doc = UIDoc.Document;
			View3D ActiveView = Doc.ActiveView as View3D;

			if (ActiveView == null)
			{
				string Message = "You must be in a 3D view to export.";
				MessageBox.Show(Message, DIALOG_CAPTION, MessageBoxButtons.OK, MessageBoxIcon.Warning);
				return Result.Cancelled;
			}

			if (ActiveView.IsTemplate || !ActiveView.CanBePrinted)
			{
				string Message = "The active 3D view cannot be exported.";
				MessageBox.Show(Message, DIALOG_CAPTION, MessageBoxButtons.OK, MessageBoxIcon.Warning);
				return Result.Cancelled;
			}

			Debug.Assert(FDirectLink.Get() != null);

			// Holding ctrl will force full sync. 
			if (FDirectLink.Get().SyncCount > 0 && (System.Windows.Forms.Control.ModifierKeys & Keys.Control) == Keys.Control)
			{
				FDirectLink.DestroyInstance(FDirectLink.Get(), InCommandData.Application.Application);
				FDirectLink.ActivateInstance(Doc);
			}

			FDatasmithRevitExportContext ExportContext = new FDatasmithRevitExportContext(
				InCommandData.Application.Application,
				Doc,
				null,
				new DatasmithRevitExportOptions(Doc),
				FDirectLink.Get());

			// Export the active 3D View to the given Unreal Datasmith file.
			using (CustomExporter Exporter = new CustomExporter(Doc, ExportContext))
			{
				try
				{
					// The export process will exclude output of geometric objects such as faces and curves,
					// but the context needs to receive the calls related to Faces or Curves to gather data.
					// The context always receive their tessellated geometry in form of polymeshes or lines.
					Exporter.IncludeGeometricObjects = true;

					// The export process should stop in case an error occurs during any of the exporting methods.
					Exporter.ShouldStopOnError = true;

#if REVIT_API_2020
					Exporter.Export(ActiveView as Autodesk.Revit.DB.View);
#else
					Exporter.Export(ActiveView);
#endif
				}
				catch (System.Exception exception)
				{
					OutCommandMessage = string.Format("Cannot export the 3D view:\n\n{0}\n\n{1}", exception.Message, exception.StackTrace);
					MessageBox.Show(OutCommandMessage, DIALOG_CAPTION, MessageBoxButtons.OK, MessageBoxIcon.Error);
					return Result.Failed;
				}
				finally
				{
					if (ExportContext.GetMessages().Count > 0)
					{
						string Messages = string.Join($"{System.Environment.NewLine}", ExportContext.GetMessages());
						DatasmithRevitApplication.SetExportMessages(Messages);
					}
				}
			}

			return Result.Succeeded;
		}
	}

	// Add-in external command Export to Unreal Datasmith. 
	[Transaction(TransactionMode.Manual)]
	public class DatasmithExportRevitCommand : DatasmithRevitCommand
	{
		class DocumentExportPathCache
		{
			public string LastExportPath = null;
			// Per-view export path
			public Dictionary<ElementId, string> ViewPaths = new Dictionary<ElementId, string>();
		};

		private static Dictionary<Document, DocumentExportPathCache> ExportPaths = new Dictionary<Document, DocumentExportPathCache>();
	
		// Implement the interface to execute the command.
		public override Result OnExecute(ExternalCommandData InCommandData, ref string OutCommandMessage, ElementSet OutElements)
		{
			UIDocument UIDoc = InCommandData.Application.ActiveUIDocument;
			Document Doc = UIDoc.Document;

			string DocumentPath = Doc.PathName;

			if (string.IsNullOrWhiteSpace(DocumentPath))
			{
				string message = "Your document must be saved on disk before exporting.";
				MessageBox.Show(message, DIALOG_CAPTION, MessageBoxButtons.OK, MessageBoxIcon.Warning);
				return Result.Cancelled;
			}

			bool ExportActiveViewOnly = true;

			// Retrieve the Unreal Datasmith export options.
			DatasmithRevitExportOptions ExportOptions = new DatasmithRevitExportOptions(Doc);
			if ((System.Windows.Forms.Control.ModifierKeys & Keys.Control) == Keys.Control)
			{
				if (ExportOptions.ShowDialog() != DialogResult.OK)
				{
					return Result.Cancelled;
				}
				ExportActiveViewOnly = false;
			}

			// Generate file path for each view.
			Dictionary<ElementId, string> FilePaths = new Dictionary<ElementId, string>();
			List<View3D> ViewsToExport = new List<View3D>();

			if (ExportActiveViewOnly)
			{
				View3D ActiveView = Doc.ActiveView as View3D;

				if (ActiveView == null)
				{
					string Message = "You must be in a 3D view to export.";
					MessageBox.Show(Message, DIALOG_CAPTION, MessageBoxButtons.OK, MessageBoxIcon.Warning);
					return Result.Cancelled;
				}

				if (ActiveView.IsTemplate || !ActiveView.CanBePrinted)
				{
					string Message = "The active 3D view cannot be exported.";
					MessageBox.Show(Message, DIALOG_CAPTION, MessageBoxButtons.OK, MessageBoxIcon.Warning);
					return Result.Cancelled;
				}

				if (!ExportPaths.ContainsKey(Doc))
				{
					ExportPaths[Doc] = new DocumentExportPathCache();
				}

				string InitialDir = null;
				string FileName = null;
				string LastFilePath = null;

				if (ExportPaths[Doc].ViewPaths.TryGetValue(ActiveView.Id, out LastFilePath))
				{
					InitialDir = Path.GetDirectoryName(LastFilePath);
					FileName = Path.GetFileName(LastFilePath);
				}
				else
				{
					LastFilePath = ExportPaths[Doc].LastExportPath;

					if (LastFilePath != null)
					{
						InitialDir = LastFilePath;
					}
					else
					{
						InitialDir = Path.GetDirectoryName(DocumentPath);
					}

					string ViewFamilyName = ActiveView.get_Parameter(BuiltInParameter.ELEM_FAMILY_PARAM).AsValueString().Replace(" ", "");
					FileName = Regex.Replace($"{Path.GetFileNameWithoutExtension(DocumentPath)}-{ViewFamilyName}-{ActiveView.Name}.udatasmith", @"\s+", "_");
				}

				SaveFileDialog Dialog = new SaveFileDialog();

				Dialog.Title            = DIALOG_CAPTION;
				Dialog.InitialDirectory = InitialDir;
				Dialog.FileName         = FileName;
				Dialog.DefaultExt       = "udatasmith";
				Dialog.Filter           = "Unreal Datasmith|*.udatasmith";
				Dialog.CheckFileExists  = false;
				Dialog.CheckPathExists  = true;
				Dialog.AddExtension     = true;
				Dialog.OverwritePrompt  = true;

				if (Dialog.ShowDialog() != DialogResult.OK)
				{
					return Result.Cancelled;
				}

				ExportPaths[Doc].LastExportPath = Path.GetDirectoryName(Dialog.FileName);
				ExportPaths[Doc].ViewPaths[ActiveView.Id] = Dialog.FileName;

				if (string.IsNullOrWhiteSpace(Dialog.FileName))
				{
					string message = "The given Unreal Datasmith file name is blank.";
					MessageBox.Show(message, DIALOG_CAPTION, MessageBoxButtons.OK, MessageBoxIcon.Warning);
					return Result.Cancelled;
				}

				FilePaths.Add(ActiveView.Id, Dialog.FileName);
				ViewsToExport.Add(ActiveView);
			}
			else
			{
				string SavePath;
				using (var FBD = new FolderBrowserDialog())
				{
					FBD.ShowNewFolderButton = true;
					DialogResult result = FBD.ShowDialog();

					if (result != DialogResult.OK || string.IsNullOrWhiteSpace(FBD.SelectedPath))
					{
						return Result.Cancelled;
					}

					SavePath = FBD.SelectedPath;
				}

				foreach (var View in ExportOptions.Selected3DViews)
				{
					string ViewFamilyName = View.get_Parameter(BuiltInParameter.ELEM_FAMILY_PARAM).AsValueString().Replace(" ", "");
					string FileName = Regex.Replace($"{Path.GetFileNameWithoutExtension(DocumentPath)}-{ViewFamilyName}-{View.Name}.udatasmith", @"\s+", "_");
					FilePaths.Add(View.Id, Path.Combine(SavePath, FileName));
					ViewsToExport.Add(View);
				}
			}

			// Prevent user interaction with the active 3D view to avoid the termination of the custom export,
			// without Revit providing any kind of internal or external feedback.
			EnableViewWindow(InCommandData.Application, false);

			// Create a custom export context for command Export to Unreal Datasmith.
			FDatasmithRevitExportContext ExportContext = new FDatasmithRevitExportContext(
				InCommandData.Application.Application,
				Doc,
				FilePaths,
				ExportOptions,
				null);

			// Export the active 3D View to the given Unreal Datasmith file.
			using( CustomExporter Exporter = new CustomExporter(Doc, ExportContext) )
			{
				// Add a progress bar callback.
				// application.ProgressChanged += exportContext.HandleProgressChanged;

				try
				{
					// The export process will exclude output of geometric objects such as faces and curves,
					// but the context needs to receive the calls related to Faces or Curves to gather data.
					// The context always receive their tessellated geometry in form of polymeshes or lines.
					Exporter.IncludeGeometricObjects = true;

					// The export process should stop in case an error occurs during any of the exporting methods.
					Exporter.ShouldStopOnError = true;

					// Initiate the export process for all 3D views.
					foreach (var view in ViewsToExport)
					{
#if REVIT_API_2020
						Exporter.Export(view as Autodesk.Revit.DB.View);
#else
						Exporter.Export(view);
#endif
					}
				}
				catch( System.Exception exception )
				{
					OutCommandMessage = string.Format("Cannot export the 3D view:\n\n{0}\n\n{1}", exception.Message, exception.StackTrace);
					MessageBox.Show(OutCommandMessage, DIALOG_CAPTION, MessageBoxButtons.OK, MessageBoxIcon.Error);
					return Result.Failed;
				}
				finally
				{
					// Remove the progress bar callback.
					// application.ProgressChanged -= exportContext.HandleProgressChanged;

					// Restore user interaction with the active 3D view.
					EnableViewWindow(InCommandData.Application, true);

					if (ExportContext.GetMessages().Count > 0)
					{
						string Messages = string.Join($"{System.Environment.NewLine}", ExportContext.GetMessages());
						DatasmithRevitApplication.SetExportMessages(Messages);
					}
				}
			}

			return Result.Succeeded;
		}

        [DllImport("user32.dll", SetLastError = true)]
        private static extern IntPtr FindWindowEx(IntPtr parentHandle, IntPtr childAfterHandle, string className, string windowTitle);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool EnableWindow(IntPtr windowHandle, bool bEnable);

        private void EnableViewWindow(
			UIApplication in_application,
			bool          in_enable
		)
        {
#if REVIT_API_2018
				IntPtr MainWindowHandle = System.Diagnostics.Process.GetCurrentProcess().MainWindowHandle;
#else
				IntPtr MainWindowHandle = in_application.MainWindowHandle;
#endif

			// "AfxFrameOrView140u" is the window class name of Revit active 3D view.
			IntPtr ViewWindowHandle = FindChildWindow(MainWindowHandle, "AfxFrameOrView140u");

            if (ViewWindowHandle != IntPtr.Zero)
			{
                EnableWindow(ViewWindowHandle, in_enable);
			}
        }

        private IntPtr FindChildWindow(
			IntPtr InParentWindowHandle,
			string InWindowClassName
		)
        {
            IntPtr WindowHandle = FindWindowEx(InParentWindowHandle, IntPtr.Zero, InWindowClassName, null);

            if (WindowHandle == IntPtr.Zero)
            {
                IntPtr WindowHandleChild = FindWindowEx(InParentWindowHandle, IntPtr.Zero, null, null);

                while (WindowHandleChild != IntPtr.Zero && WindowHandle == IntPtr.Zero)
                {
                    WindowHandle = FindChildWindow(WindowHandleChild, InWindowClassName);

					if (WindowHandle == IntPtr.Zero)
                    {
                        WindowHandleChild = FindWindowEx(InParentWindowHandle, WindowHandleChild, null, null);
                    }
                }
            }

            return WindowHandle;
        }
	}

	[Transaction(TransactionMode.Manual)]
	public class DatasmithManageConnectionsRevitCommand : IExternalCommand
	{
		public Result Execute(ExternalCommandData InCommandData, ref string OutCommandMessage, ElementSet OutElements) 
		{
			IDirectLinkUI DirectLinkUI = IDatasmithExporterUIModule.Get()?.GetDirectLinkExporterUI();
			DirectLinkUI?.OpenDirectLinkStreamWindow();
			return Result.Succeeded;
		}
	}

	[Transaction(TransactionMode.Manual)]
	public class DatasmithShowMessagesRevitCommand : IExternalCommand
	{
		public Result Execute(ExternalCommandData InCommandData, ref string OutCommandMessage, ElementSet OutElements)
		{
			DatasmithRevitApplication.ShowExportMessages();
			return Result.Succeeded;
		}
	}
}
