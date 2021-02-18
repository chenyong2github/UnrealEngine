// Copyright Epic Games, Inc. All Rights Reserved.

using Rhino;
using System;
using System.IO;

namespace DatasmithRhino.DirectLink
{
	public class DatasmithRhinoDirectLinkManager
	{
		private const string UntitledSceneName = "Untitled";

		public FDatasmithFacadeDirectLink DirectLink { get; private set; } = null;
		public FDatasmithFacadeScene DatasmithScene { get; private set; } = null;
		public DatasmithRhinoExportContext ExportContext { get; private set; } = null;
		public DatasmithRhinoChangeListener ChangeListener { get; private set; } = new DatasmithRhinoChangeListener();
		public bool bInitialized { get; private set; } = false;

		public void Initialize()
		{
			if (bInitialized)
			{
				System.Diagnostics.Debug.Fail("Calling DatasmithRhinoDirectLinkManager::Initialize() when DirectLink is already initialized.");
				return;
			}

			if (Environment.OSVersion.Platform == PlatformID.Win32NT)
			{
				string RhinoEngineDir = GetEngineDirWindows();
				if (Directory.Exists(RhinoEngineDir))
				{
					bInitialized = FDatasmithFacadeDirectLink.Init(true, RhinoEngineDir);
				}
				else
				{
					System.Diagnostics.Debug.Fail("Could not initialize FDatasmithFacadeDirectLink because of missing Engine resources");
				}
			}
			else
			{
				//Mac platform, the DatasmithExporter Slate UI is not supported for now on this platform. Simply initialize DirectLink without it.
				bInitialized = FDatasmithFacadeDirectLink.Init();
			}

			if (bInitialized)
			{
				RhinoDoc.EndOpenDocument += OnEndOpenDocument;
				RhinoDoc.BeginOpenDocument += OnBeginOpenDocument;
				RhinoDoc.NewDocument += OnNewDocument;
			}

			System.Diagnostics.Debug.Assert(bInitialized);
		}

		public void ShutDown()
		{
			if (bInitialized)
			{
				RhinoDoc.EndOpenDocument -= OnEndOpenDocument;
				RhinoDoc.NewDocument -= OnNewDocument;

				FDatasmithFacadeDirectLink.Shutdown();
				bInitialized = false;
			}
		}

		public Rhino.Commands.Result Synchronize(RhinoDoc RhinoDocument)
		{
			Rhino.Commands.Result ExportResult = Rhino.Commands.Result.Failure;
			bool bIsValidContext = RhinoDocument == ExportContext.RhinoDocument;

			if (bIsValidContext || SetupDirectLinkScene(RhinoDocument))
			{
				ExportResult = DatasmithRhinoSceneExporter.ExportScene(DatasmithScene, ExportContext, DirectLink.UpdateScene);
				ChangeListener.StartListening(ExportContext);
			}

			return ExportResult;
		}

		public bool OpenConnectionManangementWindow()
		{
			if (bInitialized && DirectLink != null)
			{
				IDirectLinkUI DirectLinkUI = IDatasmithExporterUIModule.Get()?.GetDirectLinkExporterUI();

				if (DirectLinkUI != null)
				{
					DirectLinkUI.OpenDirectLinkStreamWindow();
					return true;
				}
			}

			return false;
		}

		private string GetEngineDirWindows()
		{
			string RhinoEngineDir = null;

			try
			{
				using (Microsoft.Win32.RegistryKey Key = Microsoft.Win32.Registry.LocalMachine.OpenSubKey(@"Software\Wow6432Node\EpicGames\Unreal Engine"))
				{
					RhinoEngineDir = Key?.GetValue("RhinoEngineDir") as string;
				}
			}
			finally
			{
				if (RhinoEngineDir == null)
				{
					// If we could not read the registry, fallback to hardcoded engine dir
					RhinoEngineDir = @"C:\ProgramData\Epic\Exporter\RhinoEngine\";
				}
			}

			return RhinoEngineDir;
		}

		private void OnBeginOpenDocument(object Sender, DocumentOpenEventArgs Args)
		{
			if (!Args.Merge && !Args.Reference)
			{
				// Before opening a new document we need to stop listening to changes in the scene.
				// Otherwise we'll be updating cache of the old scene with the new document.
				ChangeListener.StopListening();
			}
		}

		private void OnEndOpenDocument(object Sender, DocumentOpenEventArgs Args)
		{
			if (!Args.Merge && !Args.Reference)
			{
				SetupDirectLinkScene(Args.Document, Args.FileName);
			}
		}

		private void OnNewDocument(object Sender, DocumentEventArgs Args)
		{
			if (Args.Document != null)
			{
				SetupDirectLinkScene(Args.Document);
				ChangeListener.StopListening();
			}
		}

		private bool SetupDirectLinkScene(RhinoDoc RhinoDocument, string FilePath = null)
		{
			//Override all of the existing scene export data, we are exporting a new document.
			try
			{
				string SceneFileName = GetSceneExportFilePath(RhinoDocument, FilePath);
				DatasmithScene = DatasmithRhinoSceneExporter.CreateDatasmithScene(SceneFileName, RhinoDocument);

				if (DirectLink == null)
				{
					DirectLink = new FDatasmithFacadeDirectLink();
				}
				DirectLink.InitializeForScene(DatasmithScene);

				const bool bSkipHidden = false;
				DatasmithRhinoExportOptions ExportOptions = new DatasmithRhinoExportOptions(RhinoDocument, DatasmithScene, bSkipHidden);
				ExportContext = new DatasmithRhinoExportContext(ExportOptions);
			}
			catch (Exception)
			{
				return false;
			}

			return true;
		}

		private string GetSceneExportFilePath(RhinoDoc RhinoDocument, string OptionalFilePath = null)
		{
			string SceneName;
			if (!string.IsNullOrEmpty(RhinoDocument.Name))
			{
				SceneName = RhinoDocument.Name;
			}
			else if (!string.IsNullOrEmpty(OptionalFilePath))
			{
				SceneName = Path.GetFileNameWithoutExtension(OptionalFilePath);
			}
			else
			{
				SceneName = UntitledSceneName;
			}

			IDirectLinkUI DirectLinkUI = IDatasmithExporterUIModule.Get()?.GetDirectLinkExporterUI();
			string ExportPath = DirectLinkUI != null
				? DirectLinkUI.GetDirectLinkCacheDirectory()
				: Path.GetTempPath();

			return Path.Combine(ExportPath, SceneName);
		}
	}
}