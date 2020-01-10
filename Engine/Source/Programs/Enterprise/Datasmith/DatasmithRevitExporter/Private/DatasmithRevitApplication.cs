// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Reflection;
using System.Windows.Media.Imaging;

using Autodesk.Revit.UI;


namespace DatasmithRevitExporter
{
	// Add-in external application Datasmith Revit Exporter.
	public class DatasmithRevitApplication : IExternalApplication
	{
		private static DatasmithRevitExportMessages ExportMessages = null;

		// Implement the interface to execute some tasks when Revit starts. 
		public Result OnStartup(
			UIControlledApplication InApplication // handle to the application being started
		)
		{
			string AssemblyPath = Assembly.GetExecutingAssembly().Location;
			PushButtonData ExportButtonData = new PushButtonData("Export3DView", "Export 3D View", AssemblyPath, "DatasmithRevitExporter.DatasmithRevitCommand");

			RibbonPanel DatasmithRibbonPanel = InApplication.CreateRibbonPanel("Unreal Datasmith");
			PushButton ExportPushButton = DatasmithRibbonPanel.AddItem(ExportButtonData) as PushButton;

			Uri DatasmithIconURI = new Uri(Path.Combine(Path.GetDirectoryName(AssemblyPath), "DatasmithIcon32.png"));

			ExportPushButton.LargeImage = new BitmapImage(DatasmithIconURI);
			ExportPushButton.ToolTip    = "Export an active 3D View to Unreal Datasmith";

			return Result.Succeeded;
		}

		// Implement the interface to execute some tasks when Revit shuts down.
		public Result OnShutdown(
			UIControlledApplication InApplication // handle to the application being shut down
		)
		{
			if (ExportMessages != null && !ExportMessages.IsDisposed)
			{
				ExportMessages.Close();
			}

			return Result.Succeeded;
		}

		public static void ShowExportMessages(
			string InMessages
		)
		{
			if (ExportMessages == null || ExportMessages.IsDisposed)
			{
				ExportMessages = new DatasmithRevitExportMessages();
				ExportMessages.Messages = InMessages;
				ExportMessages.Show();
			}
		}
	}
}
