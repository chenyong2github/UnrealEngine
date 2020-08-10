// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Windows.Media.Imaging;
using Autodesk.Revit.DB;
using Autodesk.Revit.DB.Events;
using Autodesk.Revit.UI;


namespace DatasmithRevitExporter
{
	// Add-in external application Datasmith Revit Exporter.
	public class DatasmithRevitApplication : IExternalApplication
	{
		private static DatasmithRevitExportMessages ExportMessages = null;
		private EventHandler<DocumentClosingEventArgs> DocumentClosingHandler;

		// Implement the interface to execute some tasks when Revit starts.
		public Result OnStartup(
			UIControlledApplication InApplication // handle to the application being started
		)
		{
			FDatasmithFacadeDirectLink.Init();

			string AssemblyPath = Assembly.GetExecutingAssembly().Location;
			PushButtonData ExportButtonData = new PushButtonData("Export3DView", "Export 3D View", AssemblyPath, "DatasmithRevitExporter.DatasmithExportRevitCommand");
			PushButtonData SyncButtonData = new PushButtonData("Sync3DView", "Sync 3D View", AssemblyPath, "DatasmithRevitExporter.DatasmithSyncRevitCommand");
			PushButtonData FullSyncButtonData = new PushButtonData("FullSync3DView", "Full Sync 3D View", AssemblyPath, "DatasmithRevitExporter.DatasmithFullSyncRevitCommand");

			RibbonPanel DatasmithRibbonPanel = InApplication.CreateRibbonPanel("Unreal Datasmith");
			PushButton ExportPushButton = DatasmithRibbonPanel.AddItem(ExportButtonData) as PushButton;
			PushButton SyncPushButton = DatasmithRibbonPanel.AddItem(SyncButtonData) as PushButton;
			PushButton FullSyncPushButton = DatasmithRibbonPanel.AddItem(FullSyncButtonData) as PushButton;

			Uri DatasmithExportIconURI = new Uri(Path.Combine(Path.GetDirectoryName(AssemblyPath), "DatasmithIcon32.png"));
			Uri DatasmithSyncIconURI = new Uri(Path.Combine(Path.GetDirectoryName(AssemblyPath), "DatasmithSyncIcon32.png"));

			ExportPushButton.LargeImage = new BitmapImage(DatasmithExportIconURI);
			ExportPushButton.ToolTip    = "Export an active 3D View to Unreal Datasmith";

			SyncPushButton.LargeImage = new BitmapImage(DatasmithSyncIconURI);
			SyncPushButton.ToolTip = "Sync an active 3D View with DirectLink";

			FullSyncPushButton.LargeImage = new BitmapImage(DatasmithSyncIconURI);
			FullSyncPushButton.ToolTip = "Force the sync of an active 3D View with DirectLink";

			DocumentClosingHandler = new EventHandler<DocumentClosingEventArgs>(OnDocumentClosing);
			InApplication.ControlledApplication.DocumentClosing += DocumentClosingHandler;

			return Result.Succeeded;
		}

		static void OnDocumentClosing(object sender, DocumentClosingEventArgs e)
		{
			// Make sure direct link is destroyed, if it was valid.
			if (DatasmithSyncRevitCommand.DirectLink != null)
			{
				DatasmithSyncRevitCommand.DirectLink.Destroy();
			}
			DatasmithSyncRevitCommand.DirectLink = null;
		}

		// Implement the interface to execute some tasks when Revit shuts down.
		public Result OnShutdown(
		UIControlledApplication InApplication // handle to the application being shut down
		)
		{
			InApplication.ControlledApplication.DocumentClosing -= DocumentClosingHandler;

			if (ExportMessages != null && !ExportMessages.IsDisposed)
			{
				ExportMessages.Close();
			}
			FDatasmithFacadeDirectLink.Shutdown();
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
