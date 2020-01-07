// Copyright Epic Games, Inc. All Rights Reserved.

using System;
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
	// Add-in external command Export to Unreal Datasmith. 
	[Transaction(TransactionMode.Manual)]
	public class DatasmithRevitCommand : IExternalCommand
	{
		private const string DIALOG_CAPTION = "Export 3D View to Unreal Datasmith";

		// Implement the interface to execute the command.
		public Result Execute(
			ExternalCommandData in_commandData,     // contains reference to Application and View
			ref string          out_commandMessage, // error message to display in the failure dialog when the command returns "Failed"
			ElementSet          out_elements        // set of problem elements to display in the failure dialog when the command returns "Failed"
		)
		{
			Autodesk.Revit.ApplicationServices.Application application = in_commandData.Application.Application;

			if (string.Compare(application.VersionNumber, "2018", StringComparison.Ordinal) == 0 && string.Compare(application.SubVersionNumber, "2018.3", StringComparison.Ordinal) < 0)
			{
				string message = string.Format("The running Revit is not supported.\nYou must use Revit 2018.3 or further updates to export.");
				MessageBox.Show(message, DIALOG_CAPTION, MessageBoxButtons.OK, MessageBoxIcon.Warning);
				return Result.Cancelled;
			}

			if (!CustomExporter.IsRenderingSupported())
			{
				string message = "3D view rendering is not supported in the running Revit.";
				MessageBox.Show(message, DIALOG_CAPTION, MessageBoxButtons.OK, MessageBoxIcon.Warning);
				return Result.Cancelled;
			}

			UIDocument uiDocument = in_commandData.Application.ActiveUIDocument;

			if (uiDocument == null)
			{
				string message = "You must be in a document to export.";
				MessageBox.Show(message, DIALOG_CAPTION, MessageBoxButtons.OK, MessageBoxIcon.Warning);
				return Result.Cancelled;
			}

			Document document = uiDocument.Document;
	
			View3D activeView = document.ActiveView as View3D;

			if (activeView == null)
			{
				string message = "You must be in a 3D view to export.";
				MessageBox.Show(message, DIALOG_CAPTION, MessageBoxButtons.OK, MessageBoxIcon.Warning);
				return Result.Cancelled;
			}

			if (activeView.IsTemplate || !activeView.CanBePrinted)
			{
				string message = "The active 3D view cannot be exported.";
				MessageBox.Show(message, DIALOG_CAPTION, MessageBoxButtons.OK, MessageBoxIcon.Warning);
				return Result.Cancelled;
			}

			string documentPath = document.PathName;

			if (string.IsNullOrWhiteSpace(documentPath))
			{
				string message = "Your document must be saved on disk before exporting.";
				MessageBox.Show(message, DIALOG_CAPTION, MessageBoxButtons.OK, MessageBoxIcon.Warning);
				return Result.Cancelled;
			}

			string ViewFamilyName = activeView.get_Parameter(BuiltInParameter.ELEM_FAMILY_PARAM).AsValueString().Replace(" ", "");
			string FileName       = Regex.Replace($"{Path.GetFileNameWithoutExtension(documentPath)}-{ViewFamilyName}-{activeView.Name}.udatasmith", @"\s+", "_");

			SaveFileDialog dialog = new SaveFileDialog();

			dialog.Title            = DIALOG_CAPTION;
			dialog.InitialDirectory = Path.GetDirectoryName(documentPath);
			dialog.FileName         = FileName;
			dialog.DefaultExt       = "udatasmith";
			dialog.Filter           = "Unreal Datasmith|*.udatasmith";
			dialog.CheckFileExists  = false;
			dialog.CheckPathExists  = true;
			dialog.AddExtension     = true;
			dialog.OverwritePrompt  = true;

			if (dialog.ShowDialog() != DialogResult.OK)
			{
				return Result.Cancelled;
			}

			string filePath = dialog.FileName;

			if (string.IsNullOrWhiteSpace(filePath))
			{
				string message = "The given Unreal Datasmith file name is blank.";
				MessageBox.Show(message, DIALOG_CAPTION, MessageBoxButtons.OK, MessageBoxIcon.Warning);
				return Result.Cancelled;
			}

			// Retrieve the Unreal Datasmith export options.
			DatasmithRevitExportOptions exportOptions = new DatasmithRevitExportOptions();
			if ((System.Windows.Forms.Control.ModifierKeys & Keys.Control) == Keys.Control)
			{
				if (exportOptions.ShowDialog() != DialogResult.OK)
				{
					return Result.Cancelled;
				}
			}

			// Prevent user interaction with the active 3D view to avoid the termination of the custom export,
			// without Revit providing any kind of internal or external feedback.
			EnableViewWindow(in_commandData.Application, false);

			// Create a custom export context for command Export to Unreal Datasmith.
			FDatasmithRevitExportContext exportContext = new FDatasmithRevitExportContext(in_commandData.Application.Application, document, filePath, exportOptions);

			// Export the active 3D View to the given Unreal Datasmith file.
			using( CustomExporter customExporter = new CustomExporter(document, exportContext) )
			{
				// Add a progress bar callback.
				// application.ProgressChanged += exportContext.HandleProgressChanged;

				try
				{
					// The export process will exclude output of geometric objects such as faces and curves,
					// but the context needs to receive the calls related to Faces or Curves to gather data.
					// The context always receive their tessellated geometry in form of polymeshes or lines.
					customExporter.IncludeGeometricObjects = true;

					// The export process should stop in case an error occurs during any of the exporting methods.
					customExporter.ShouldStopOnError = true;

					// Initiate the export process for the active 3D View.
					#if REVIT_API_2020
						customExporter.Export(activeView as Autodesk.Revit.DB.View);
					#else
						customExporter.Export(activeView);
					#endif
				}
				catch( System.Exception exception )
				{
					out_commandMessage = string.Format("Cannot export the active 3D view:\n\n{0}\n\n{1}", exception.Message, exception.StackTrace);
					MessageBox.Show(out_commandMessage, DIALOG_CAPTION, MessageBoxButtons.OK, MessageBoxIcon.Error);
					return Result.Failed;
				}
				finally
				{
					// Remove the progress bar callback.
					// application.ProgressChanged -= exportContext.HandleProgressChanged;

					// Restore user interaction with the active 3D view.
					EnableViewWindow(in_commandData.Application, true);

					if (exportContext.GetMessages().Count > 0)
					{
						string Messages = string.Join($"{System.Environment.NewLine}", exportContext.GetMessages());
						DatasmithRevitApplication.ShowExportMessages(Messages);
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
				IntPtr mainWindowHandle = Process.GetCurrentProcess().MainWindowHandle;
			#else
				IntPtr mainWindowHandle = in_application.MainWindowHandle;
			#endif

			// "AfxFrameOrView140u" is the window class name of Revit active 3D view.
			IntPtr viewWindowHandle = FindChildWindow(mainWindowHandle, "AfxFrameOrView140u");

            if (viewWindowHandle != IntPtr.Zero)
			{
                EnableWindow(viewWindowHandle, in_enable);
			}
        }

        private IntPtr FindChildWindow(
			IntPtr in_parentWindowHandle,
			string in_windowClassName
		)
        {
            IntPtr windowHandle = FindWindowEx(in_parentWindowHandle, IntPtr.Zero, in_windowClassName, null);
			
            if (windowHandle == IntPtr.Zero)
            {
                IntPtr windowHandleChild = FindWindowEx(in_parentWindowHandle, IntPtr.Zero, null, null);

                while (windowHandleChild != IntPtr.Zero && windowHandle == IntPtr.Zero)
                {
                    windowHandle = FindChildWindow(windowHandleChild, in_windowClassName);
					
                    if (windowHandle == IntPtr.Zero)
                    {
                        windowHandleChild = FindWindowEx(in_parentWindowHandle, windowHandleChild, null, null);
                    }
                }
            }
			
            return windowHandle;
        }
	}
}
