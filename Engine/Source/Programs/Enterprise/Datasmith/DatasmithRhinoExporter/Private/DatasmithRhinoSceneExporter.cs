// Copyright Epic Games, Inc. All Rights Reserved.

using DatasmithRhino.ElementExporters;
using Rhino;
using Rhino.DocObjects;
using Rhino.Geometry;
using System;
using System.Collections.Specialized;

namespace DatasmithRhino
{
	// Exception thrown when the user cancels.
	public class DatasmithExportCancelledException : Exception
	{
	}

	public static class DatasmithRhinoSceneExporter
	{
		public static Rhino.PlugIns.WriteFileResult Export(RhinoDoc RhinoDocument, FDatasmithRhinoExportOptions Options)
		{
			try
			{
				RhinoApp.WriteLine(string.Format("Exporting to {0}.", System.IO.Path.GetFileName(Options.DestinationFileName)));
				RhinoApp.WriteLine("Press Esc key to cancel...");

				FDatasmithFacadeScene DatasmithScene = SetUpSceneExport(Options.DestinationFileName, RhinoDocument);

				FDatasmithRhinoProgressManager.Instance.StartMainTaskProgress("Parsing Document", 0.1f);
				DatasmithRhinoSceneParser SceneParser = new DatasmithRhinoSceneParser(RhinoDocument, Options);
				SceneParser.ParseDocument();

				if (SynchronizeScene(SceneParser, DatasmithScene) == Rhino.Commands.Result.Success)
				{
					FDatasmithRhinoProgressManager.Instance.StartMainTaskProgress("Writing to files..", 1);
					DatasmithScene.ExportScene();
				}
			}
			catch (DatasmithExportCancelledException)
			{
				return Rhino.PlugIns.WriteFileResult.Cancel;
			}
			catch (Exception e)
			{
				RhinoApp.WriteLine("An unexpected error has occurred:");
				RhinoApp.WriteLine(e.ToString());
				return Rhino.PlugIns.WriteFileResult.Failure;
			}
			finally
			{
				FDatasmithRhinoProgressManager.Instance.StopProgress();
			}

			return Rhino.PlugIns.WriteFileResult.Success;
		}

		public static Rhino.Commands.Result ExportDirectLink(ref FDatasmithFacadeScene DatasmithScene, ref FDatasmithFacadeDirectLink DirectLink, RhinoDoc RhinoDocument)
		{
			try
			{
				string DocumentName = System.IO.Path.Combine(System.IO.Path.GetTempPath(), RhinoDocument.Name);
				RhinoApp.WriteLine(string.Format("Exporting to {0}.", System.IO.Path.GetFileName(DocumentName)));
				RhinoApp.WriteLine("Press Esc key to cancel...");

				if (DatasmithScene == null)
				{
					DatasmithScene = SetUpSceneExport(DocumentName, RhinoDocument);
				}

				if (DirectLink == null)
				{
					DirectLink = new FDatasmithFacadeDirectLink();
					DirectLink.InitializeForScene(DatasmithScene);
				}

				FDatasmithRhinoProgressManager.Instance.StartMainTaskProgress("Parsing Document", 0.1f);
				FDatasmithRhinoExportOptions Options = new FDatasmithRhinoExportOptions(DocumentName);
				DatasmithRhinoSceneParser SceneParser = new DatasmithRhinoSceneParser(RhinoDocument, Options);
				SceneParser.ParseDocument();

				if (SynchronizeScene(SceneParser, DatasmithScene) == Rhino.Commands.Result.Success)
				{
					FDatasmithRhinoProgressManager.Instance.StartMainTaskProgress("Broadcasting Datasmith Scene..", 1);
					DirectLink.UpdateScene(DatasmithScene);
				}
			}
			catch (DatasmithExportCancelledException)
			{
				return Rhino.Commands.Result.Cancel;
			}
			catch (Exception e)
			{
				RhinoApp.WriteLine("An unexpected error has occurred:");
				RhinoApp.WriteLine(e.ToString());
				return Rhino.Commands.Result.Failure;
			}
			finally
			{
				FDatasmithRhinoProgressManager.Instance.StopProgress();
			}

			return Rhino.Commands.Result.Success;
		}

		public static FDatasmithFacadeScene SetUpSceneExport(string Filename, RhinoDoc RhinoDocument)
		{
			string RhinoAppName = Rhino.RhinoApp.Name;
			string RhinoVersion = Rhino.RhinoApp.ExeVersion.ToString();
			FDatasmithFacadeElement.SetCoordinateSystemType(FDatasmithFacadeElement.ECoordinateSystemType.RightHandedZup);
			FDatasmithFacadeElement.SetWorldUnitScale((float)Rhino.RhinoMath.UnitScale(RhinoDocument.ModelUnitSystem, UnitSystem.Centimeters));
			FDatasmithFacadeScene DatasmithScene = new FDatasmithFacadeScene("Rhino", "Robert McNeel & Associates", "Rhino3D", RhinoVersion);
			DatasmithScene.PreExport();
			DatasmithScene.SetOutputPath(System.IO.Path.GetDirectoryName(Filename));
			DatasmithScene.SetName(System.IO.Path.GetFileNameWithoutExtension(Filename));

			return DatasmithScene;
		}

		public static Rhino.Commands.Result SynchronizeScene(DatasmithRhinoSceneParser SceneParser, FDatasmithFacadeScene DatasmithScene)
		{
			FDatasmithRhinoProgressManager.Instance.StartMainTaskProgress("Exporting Textures", 0.1f);
			DatasmithRhinoTextureExporter.Instance.SynchronizeElements(DatasmithScene, SceneParser);

			FDatasmithRhinoProgressManager.Instance.StartMainTaskProgress("Exporting Materials", 0.2f);
			DatasmithRhinoMaterialExporter.Instance.SynchronizeElements(DatasmithScene, SceneParser);

			FDatasmithRhinoProgressManager.Instance.StartMainTaskProgress("Exporting Meshes", 0.7f);
			DatasmithRhinoMeshExporter.Instance.SynchronizeElements(DatasmithScene, SceneParser);

			FDatasmithRhinoProgressManager.Instance.StartMainTaskProgress("Exporting Actors", 0.8f);
			DatasmithRhinoActorExporter.Instance.SynchronizeElements(DatasmithScene, SceneParser);

			return Rhino.Commands.Result.Success;
		}
	}
}