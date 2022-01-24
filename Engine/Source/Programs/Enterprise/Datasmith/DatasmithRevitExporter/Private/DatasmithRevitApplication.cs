// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Windows.Media.Imaging;
using Autodesk.Revit.DB;
using Autodesk.Revit.DB.Events;
using Autodesk.Revit.UI;
using Autodesk.Revit.UI.Events;
using Microsoft.Win32;

namespace DatasmithRevitExporter
{
	// Add-in external application Datasmith Revit Exporter.
	public class DatasmithRevitApplication : IExternalApplication
	{
		private static DatasmithRevitExportMessages ExportMessagesDialog = null;

		private static string ExportMessages;

		private EventHandler<DocumentClosingEventArgs> DocumentClosingHandler;
		private EventHandler<ViewActivatedEventArgs> ViewActivatedHandler;

		//PushButton AutoSyncPushButton;
		PushButton SyncPushButton;

		//BitmapImage AutoSyncIconOn_Small;
		//BitmapImage AutoSyncIconOn_Large;
		//BitmapImage AutoSyncIconOff_Small;
		//BitmapImage AutoSyncIconOff_Large;

		public object Properties { get; private set; }

		public static DatasmithRevitApplication Instance { get; private set; }

#if false
		public void SetAutoSyncButtonToggled(bool bToggled)
		{
			if (bToggled)
			{
				AutoSyncPushButton.Image = AutoSyncIconOff_Small;
				AutoSyncPushButton.LargeImage = AutoSyncIconOff_Large;
			}
			else
			{
				AutoSyncPushButton.Image = AutoSyncIconOn_Small;
				AutoSyncPushButton.LargeImage = AutoSyncIconOn_Large;
			}
			SyncPushButton.Enabled = !bToggled;
		}
#endif
		// Implement the interface to execute some tasks when Revit starts.
		public Result OnStartup(
			UIControlledApplication InApplication // handle to the application being started
		)
		{
			Instance = this;

			// Create a custom ribbon tab
			string TabName = DatasmithRevitResources.Strings.DatasmithTabName;
			InApplication.CreateRibbonTab(TabName);

			// Add a new ribbon panel
			RibbonPanel DirectLinkRibbonPanel = InApplication.CreateRibbonPanel(TabName, DatasmithRevitResources.Strings.RibbonSection_DirectLink);
			RibbonPanel FileExportRibbonPanel = InApplication.CreateRibbonPanel(TabName, DatasmithRevitResources.Strings.RibbonSection_FileExport);
			RibbonPanel DatasmithRibbonPanel = InApplication.CreateRibbonPanel(TabName, DatasmithRevitResources.Strings.RibbonSection_Datasmith);

			string AssemblyPath = Assembly.GetExecutingAssembly().Location;
			PushButtonData ExportButtonData = new PushButtonData("Export3DView", DatasmithRevitResources.Strings.ButtonExport3DView, AssemblyPath, "DatasmithRevitExporter.DatasmithExportRevitCommand");
			PushButtonData SyncButtonData = new PushButtonData("Sync3DView", DatasmithRevitResources.Strings.ButtonSync, AssemblyPath, "DatasmithRevitExporter.DatasmithSyncRevitCommand");
			//PushButtonData AutoSyncButtonData = new PushButtonData("AutoSync3DView", DatasmithRevitResources.Strings.ButtonAutoSync, AssemblyPath, "DatasmithRevitExporter.DatasmithAutoSyncRevitCommand");
			PushButtonData ManageConnectionsButtonData = new PushButtonData("Connections", DatasmithRevitResources.Strings.ButtonConnections, AssemblyPath, "DatasmithRevitExporter.DatasmithManageConnectionsRevitCommand");
			PushButtonData SettingsButtonData = new PushButtonData("Settings", DatasmithRevitResources.Strings.ButtonSettings, AssemblyPath, "DatasmithRevitExporter.DatasmithShowSettingsRevitCommand");
			PushButtonData LogButtonData = new PushButtonData("Messages", DatasmithRevitResources.Strings.ButtonMessages, AssemblyPath, "DatasmithRevitExporter.DatasmithShowMessagesRevitCommand");

			SyncPushButton = DirectLinkRibbonPanel.AddItem(SyncButtonData) as PushButton;
			//AutoSyncPushButton = DirectLinkRibbonPanel.AddItem(AutoSyncButtonData) as PushButton;
			PushButton ManageConnectionsButton = DirectLinkRibbonPanel.AddItem(ManageConnectionsButtonData) as PushButton;
			PushButton ExportPushButton = FileExportRibbonPanel.AddItem(ExportButtonData) as PushButton;
			PushButton ShowLogButton = DatasmithRibbonPanel.AddItem(LogButtonData) as PushButton;
			PushButton SettingsButton = DatasmithRibbonPanel.AddItem(SettingsButtonData) as PushButton;

			string DatasmithIconBase = Path.Combine(Path.GetDirectoryName(AssemblyPath), "DatasmithIcon");
			ExportPushButton.Image = new BitmapImage(new Uri(DatasmithIconBase + "16.png"));
			ExportPushButton.LargeImage = new BitmapImage(new Uri(DatasmithIconBase + "32.png"));
			ExportPushButton.ToolTip = DatasmithRevitResources.Strings.ButtonExport3DViewHint;

			DatasmithIconBase = Path.Combine(Path.GetDirectoryName(AssemblyPath), "DatasmithSyncIcon");
			SyncPushButton.Image = new BitmapImage(new Uri(DatasmithIconBase + "16.png"));
			SyncPushButton.LargeImage = new BitmapImage(new Uri(DatasmithIconBase + "32.png"));
			SyncPushButton.ToolTip = DatasmithRevitResources.Strings.ButtonSyncHint;

			DatasmithIconBase = Path.Combine(Path.GetDirectoryName(AssemblyPath), "DatasmithAutoSyncIcon");

			//AutoSyncIconOn_Small = new BitmapImage(new Uri(DatasmithIconBase + "On16.png"));
			//AutoSyncIconOn_Large = new BitmapImage(new Uri(DatasmithIconBase + "On32.png"));
			//AutoSyncIconOff_Small = new BitmapImage(new Uri(DatasmithIconBase + "Off16.png"));
			//AutoSyncIconOff_Large = new BitmapImage(new Uri(DatasmithIconBase + "Off32.png"));

			//AutoSyncPushButton.Image = AutoSyncIconOn_Small;
			//AutoSyncPushButton.LargeImage = AutoSyncIconOn_Large;
			//AutoSyncPushButton.ToolTip = DatasmithRevitResources.Strings.ButtonAutoSyncHint;

			DatasmithIconBase = Path.Combine(Path.GetDirectoryName(AssemblyPath), "DatasmithManageConnectionsIcon");
			ManageConnectionsButton.Image = new BitmapImage(new Uri(DatasmithIconBase + "16.png"));
			ManageConnectionsButton.LargeImage = new BitmapImage(new Uri(DatasmithIconBase + "32.png"));
			ManageConnectionsButton.ToolTip = DatasmithRevitResources.Strings.ButtonConnectionsHint;

			DatasmithIconBase = Path.Combine(Path.GetDirectoryName(AssemblyPath), "DatasmithSettingsIcon");
			SettingsButton.Image = new BitmapImage(new Uri(DatasmithIconBase + "16.png"));
			SettingsButton.LargeImage = new BitmapImage(new Uri(DatasmithIconBase + "32.png"));
			SettingsButton.ToolTip = DatasmithRevitResources.Strings.ButtonSettingsHint;

			DatasmithIconBase = Path.Combine(Path.GetDirectoryName(AssemblyPath), "DatasmithLogIcon");
			ShowLogButton.Image = new BitmapImage(new Uri(DatasmithIconBase + "16.png"));
			ShowLogButton.LargeImage = new BitmapImage(new Uri(DatasmithIconBase + "32.png"));
			ShowLogButton.ToolTip = DatasmithRevitResources.Strings.ButtonMessagesHint;

			DocumentClosingHandler = new EventHandler<DocumentClosingEventArgs>(OnDocumentClosing);
			InApplication.ControlledApplication.DocumentClosing += DocumentClosingHandler;

			ViewActivatedHandler = new EventHandler<ViewActivatedEventArgs>(OnViewActivated);
			InApplication.ViewActivated += ViewActivatedHandler;
			
			// Setup Direct Link

			string RevitEngineDir = null;

			try
			{
				using (RegistryKey Key = Registry.LocalMachine.OpenSubKey("Software\\Wow6432Node\\EpicGames\\Unreal Engine"))
				{
					RevitEngineDir = Key?.GetValue("RevitEngineDir") as string;
				}
			}
			finally
			{
				if (RevitEngineDir == null)
				{
					// If we could not read the registry, fallback to hardcoded engine dir
					RevitEngineDir = "C:\\ProgramData\\Epic\\Exporter\\RevitEngine\\";
				}
			}

			bool bDirectLinkInitOk = FDatasmithFacadeDirectLink.Init(true, RevitEngineDir);

			Debug.Assert(bDirectLinkInitOk);

			FSettingsManager.Init(InApplication);

			return Result.Succeeded;
		}

		static void OnDocumentClosing(object sender, DocumentClosingEventArgs e)
		{
			FDirectLink.DestroyInstance(FDirectLink.FindInstance(e.Document), e.Document.Application);
		}

		static void OnViewActivated(object sender, ViewActivatedEventArgs e)
		{
			View Previous = e.PreviousActiveView;
			View Current = e.CurrentActiveView;

			if (Previous == null || !Previous.Document.Equals(Current.Document))
			{
				FDirectLink.ActivateInstance(Current.Document);
			}
		}

		// Implement the interface to execute some tasks when Revit shuts down.
		public Result OnShutdown(
			UIControlledApplication InApplication // handle to the application being shut down
		)
		{
			InApplication.ControlledApplication.DocumentClosing -= DocumentClosingHandler;
			InApplication.ViewActivated -= ViewActivatedHandler;

			DocumentClosingHandler = null;
			ViewActivatedHandler = null;

			if (ExportMessagesDialog != null && !ExportMessagesDialog.IsDisposed)
			{
				ExportMessagesDialog.Close();
			}
			FSettingsManager.Destroy(InApplication);
			FDatasmithFacadeDirectLink.Shutdown();
			return Result.Succeeded;
		}

		public static void SetExportMessages(string InMessages)
		{
			ExportMessages = InMessages;

			if (ExportMessagesDialog != null)
			{
				ExportMessagesDialog.Messages = ExportMessages;
			}
		}

		public static void ShowExportMessages()
		{
			if (ExportMessagesDialog == null || ExportMessagesDialog.IsDisposed)
			{
				ExportMessagesDialog = new DatasmithRevitExportMessages(() => ExportMessages = "");
				ExportMessagesDialog.Messages = ExportMessages;
				ExportMessagesDialog.Show();
			}
			else
			{
				ExportMessagesDialog.Focus();
			}
		}
	}
}
