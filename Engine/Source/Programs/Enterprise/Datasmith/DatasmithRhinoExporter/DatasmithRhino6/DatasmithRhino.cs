// Copyright Epic Games, Inc. All Rights Reserved.

using Rhino;
using Rhino.PlugIns;
using System;
using System.Collections.Generic;

namespace DatasmithRhino
{
	///<summary>
	/// <para>Every RhinoCommon .rhp assembly must have one and only one PlugIn-derived
	/// class. DO NOT create instances of this class yourself. It is the
	/// responsibility of Rhino to create an instance of this class.</para>
	/// <para>To complete plug-in information, please also see all PlugInDescription
	/// attributes in AssemblyInfo.cs (you might need to click "Project" ->
	/// "Show All Files" to see it in the "Solution Explorer" window).</para>
	///</summary>
	public class DatasmithRhino6 : Rhino.PlugIns.FileExportPlugIn
	{
		public override PlugInLoadTime LoadTime { get { return PlugInLoadTime.AtStartup; } }
		public FDatasmithFacadeScene DatasmithScene = null;
		public FDatasmithFacadeDirectLink DirectLink = null;

		public DatasmithRhino6()
		{
			Instance = this;
			InitializeDirectLink();

			Rhino.RhinoDoc.EndOpenDocument += OnEndOpenDocument;
			AppDomain.CurrentDomain.ProcessExit += OnProcessExit;
		}

		private void OnEndOpenDocument(object Sender, DocumentOpenEventArgs Args)
		{
			const bool bNewScene = true;
			if (!Args.Merge && !Args.Reference)
			{
				SetupDirectLinkScene(Args.Document, Args.FileName, bNewScene);
			}
		}

		private void InitializeDirectLink()
		{
			string RhinoEngineDir = Environment.OSVersion.Platform == PlatformID.Win32NT ? GetEngineDirWindows() : GetEngineDirMac();
			bool bDirectLinkInitOk = FDatasmithFacadeDirectLink.Init(true, RhinoEngineDir);

			System.Diagnostics.Debug.Assert(bDirectLinkInitOk);
		}

		private void SetupDirectLinkScene(RhinoDoc RhinoDocument, string FilePath, bool bNewScene)
		{
			try
			{
				string DocumentName = "Untitled";
				if(!string.IsNullOrEmpty(RhinoDocument.Name))
				{
					DocumentName = System.IO.Path.Combine(System.IO.Path.GetTempPath(), RhinoDocument.Name);
				}
				else if(!string.IsNullOrEmpty(FilePath))
				{
					DocumentName = System.IO.Path.Combine(System.IO.Path.GetTempPath(), System.IO.Path.GetFileNameWithoutExtension(FilePath));
				}

				if (bNewScene || DatasmithScene == null)
				{
					DatasmithScene = DatasmithRhinoSceneExporter.SetUpSceneExport(DocumentName, RhinoDocument);
				}

				if (DirectLink == null)
				{
					DirectLink = new FDatasmithFacadeDirectLink();
				}

				DirectLink.InitializeForScene(DatasmithScene);
			}
			catch (Exception)
			{
			}
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

		private string GetEngineDirMac()
		{
			string RhinoPluginFolder = System.IO.Path.GetDirectoryName(new Uri(System.Reflection.Assembly.GetExecutingAssembly().CodeBase).LocalPath);

			return System.IO.Path.Combine(RhinoPluginFolder, "Resources", "RhinoEngine");
		}

		///<summary>Gets the only instance of the DatasmithRhino6 plug-in.</summary>
		public static DatasmithRhino6 Instance {
			get; private set;
		}

		/// <summary>Defines file extensions that this export plug-in is designed to write.</summary>
		/// <param name="options">Options that specify how to write files.</param>
		/// <returns>A list of file types that can be exported.</returns>
		protected override Rhino.PlugIns.FileTypeList AddFileTypes(Rhino.FileIO.FileWriteOptions options)
		{
			var result = new Rhino.PlugIns.FileTypeList();
			result.AddFileType("Unreal Datasmith (*.udatasmith)", "udatasmith");
			return result;
		}

		/// <summary>
		/// Is called when a user requests to export a ".udatasmith" file.
		/// It is actually up to this method to write the file itself.
		/// </summary>
		/// <param name="filename">The complete path to the new file.</param>
		/// <param name="index">The index of the file type as it had been specified by the AddFileTypes method.</param>
		/// <param name="doc">The document to be written.</param>
		/// <param name="options">Options that specify how to write file.</param>
		/// <returns>A value that defines success or a specific failure.</returns>
		protected override Rhino.PlugIns.WriteFileResult WriteFile(string filename, int index, RhinoDoc doc, Rhino.FileIO.FileWriteOptions options)
		{
			FDatasmithRhinoExportOptions ExportOptions = new FDatasmithRhinoExportOptions(options, filename);
			return DatasmithRhinoSceneExporter.Export(doc, ExportOptions);
		}

		public void OnProcessExit(object sender, EventArgs e)
		{
			AppDomain.CurrentDomain.ProcessExit -= OnProcessExit;

			FDatasmithFacadeDirectLink.Shutdown();

			// If we are not on Windows, we need to manually call FDatasmithFacadeScene.Shutdown() when the process ends.
			if (Environment.OSVersion.Platform != PlatformID.Win32NT)
			{
				FDatasmithFacadeScene.Shutdown();
			}
		}
	}
}