// Copyright Epic Games, Inc. All Rights Reserved.

using DatasmithRhino.DirectLink;
using DatasmithRhino.ElementExporters;
using DatasmithRhino.Utils;
using Rhino;
using System;

namespace DatasmithRhino
{
	public static class DatasmithRhinoSceneExporter
	{
		public static FDatasmithFacadeScene CreateDatasmithScene(string Filename, RhinoDoc RhinoDocument)
		{
			string RhinoAppName = RhinoApp.Name;
			string RhinoVersion = RhinoApp.ExeVersion.ToString();
			FDatasmithFacadeElement.SetCoordinateSystemType(FDatasmithFacadeElement.ECoordinateSystemType.RightHandedZup);
			FDatasmithFacadeElement.SetWorldUnitScale((float)RhinoMath.UnitScale(RhinoDocument.ModelUnitSystem, UnitSystem.Centimeters));
			FDatasmithFacadeScene DatasmithScene = new FDatasmithFacadeScene("Rhino", "Robert McNeel & Associates", "Rhino3D", RhinoVersion);
			DatasmithScene.SetOutputPath(System.IO.Path.GetDirectoryName(Filename));
			DatasmithScene.SetName(System.IO.Path.GetFileNameWithoutExtension(Filename));

			return DatasmithScene;
		}

		public static Rhino.PlugIns.WriteFileResult ExportToFile(DatasmithRhinoExportOptions Options)
		{
			Func<FDatasmithFacadeScene, bool> OnSceneExportCompleted = (FDatasmithFacadeScene Scene) => { return Scene.ExportScene(); };
			DatasmithRhinoExportContext ExportContext = new DatasmithRhinoExportContext(Options);

			Rhino.Commands.Result ExportResult = ExportScene(Options.DatasmithScene, ExportContext, OnSceneExportCompleted);

			//Return with the corresponding WriteFileResult;
			switch (ExportResult)
			{
				case Rhino.Commands.Result.Success:
					return Rhino.PlugIns.WriteFileResult.Success;
				case Rhino.Commands.Result.Cancel:
				case Rhino.Commands.Result.CancelModelessDialog:
					return Rhino.PlugIns.WriteFileResult.Cancel;
				case Rhino.Commands.Result.Failure:
				default:
					return Rhino.PlugIns.WriteFileResult.Failure;
			}
		}

		public static Rhino.Commands.Result ExportToDirectLink(DatasmithRhinoDirectLinkManager DirectLinkManager)
		{
			FDatasmithFacadeScene DatasmithScene = DirectLinkManager.DatasmithScene;
			//#ueent-todo Reuse and update the DirectLinkManager context for successive exports. For now we simply create a new context to avoid crashing on iterative export.
			DatasmithRhinoExportContext ExportContext = new DatasmithRhinoExportContext(DirectLinkManager.ExportContext.ExportOptions);
			FDatasmithFacadeDirectLink DirectLinkInstance = DirectLinkManager.DirectLink;


			return ExportScene(DatasmithScene, ExportContext, DirectLinkInstance.UpdateScene);
		}

		private static Rhino.Commands.Result ExportScene(FDatasmithFacadeScene DatasmithScene, DatasmithRhinoExportContext ExportContext, Func<FDatasmithFacadeScene, bool> OnSceneExportCompleted)
		{
			bool bExportSuccess = false;
			try
			{
				RhinoApp.WriteLine(string.Format("Exporting to {0} datasmith scene.", System.IO.Path.GetFileName(DatasmithScene.GetName())));
				RhinoApp.WriteLine("Press Esc key to cancel...");

				DatasmithScene.PreExport();

				DatasmithRhinoProgressManager.Instance.StartMainTaskProgress("Parsing Document", 0.1f);
				ExportContext.ParseDocument();

				if (SynchronizeScene(ExportContext, DatasmithScene) == Rhino.Commands.Result.Success)
				{
					DatasmithRhinoProgressManager.Instance.StartMainTaskProgress("Exporting Scene..", 1);
					bExportSuccess = OnSceneExportCompleted(DatasmithScene);
				}
			}
			catch (DatasmithExportCancelledException)
			{
				return Rhino.Commands.Result.Cancel;
			}
			catch (Exception e)
			{
				bExportSuccess = false;
				RhinoApp.WriteLine("An unexpected error has occurred:");
				RhinoApp.WriteLine(e.ToString());
			}
			finally
			{
				DatasmithRhinoProgressManager.Instance.StopProgress();
			}

			return bExportSuccess
				? Rhino.Commands.Result.Success
				: Rhino.Commands.Result.Failure;
		}

		private static Rhino.Commands.Result SynchronizeScene(DatasmithRhinoExportContext ExportContext, FDatasmithFacadeScene DatasmithScene)
		{
			DatasmithRhinoProgressManager.Instance.StartMainTaskProgress("Exporting Textures", 0.1f);
			DatasmithRhinoTextureExporter.Instance.SynchronizeElements(DatasmithScene, ExportContext);

			DatasmithRhinoProgressManager.Instance.StartMainTaskProgress("Exporting Materials", 0.2f);
			DatasmithRhinoMaterialExporter.Instance.SynchronizeElements(DatasmithScene, ExportContext);

			DatasmithRhinoProgressManager.Instance.StartMainTaskProgress("Exporting Meshes", 0.7f);
			DatasmithRhinoMeshExporter.Instance.SynchronizeElements(DatasmithScene, ExportContext);

			DatasmithRhinoProgressManager.Instance.StartMainTaskProgress("Exporting Actors", 0.8f);
			DatasmithRhinoActorExporter.Instance.SynchronizeElements(DatasmithScene, ExportContext);

			return Rhino.Commands.Result.Success;
		}
	}
}