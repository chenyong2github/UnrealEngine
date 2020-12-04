// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using Rhino;
using Rhino.Commands;

namespace DatasmithRhino.Commands
{
	/**
	 * Command used to export the scene to a .udatasmith file.
	 */
	public class DatasmithExportCommand : Command
	{
		public DatasmithExportCommand()
		{
			// Rhino only creates one instance of each command class defined in a
			// plug-in, so it is safe to store a reference in a static property.
			Instance = this;
		}

		/**
		 * The only instance of this command.
		 */
		public static DatasmithExportCommand Instance {
			get; private set;
		}

		/**
		 * The command name as it appears on the Rhino command line.
		 */
		public override string EnglishName {
			get { return "DatasmithExport"; }
		}

		///TODO: This needs to be localized.
		public override string LocalName {
			get { return "DatasmithExport"; }
		}

		protected override Result RunCommand(RhinoDoc doc, RunMode mode)
		{
			Result CommandResult = Rhino.Commands.Result.Failure;

			Eto.Forms.SaveFileDialog SaveDialog = new Eto.Forms.SaveFileDialog();
			Eto.Forms.FileFilter DatasmithFileFilter = new Eto.Forms.FileFilter("Unreal Datasmith", new string[] { ".udatasmith" });
			SaveDialog.Filters.Add(DatasmithFileFilter);
			SaveDialog.CurrentFilter = DatasmithFileFilter;
			SaveDialog.Title = "Export to Datasmith Scene";
			SaveDialog.FileName = string.IsNullOrEmpty(doc.Name) ? "Untitled" : System.IO.Path.GetFileNameWithoutExtension(doc.Name);
			if(!string.IsNullOrEmpty(doc.Path))
			{
				Uri PathUri = new Uri(System.IO.Path.GetDirectoryName(doc.Path));
				SaveDialog.Directory = PathUri;
			}

			Eto.Forms.DialogResult SaveDialogResult = SaveDialog.ShowDialog(Rhino.UI.RhinoEtoApp.MainWindow);
			if (SaveDialogResult == Eto.Forms.DialogResult.Ok)
			{
				string FileName = SaveDialog.FileName;
				FDatasmithRhinoExportOptions ExportOptions = new FDatasmithRhinoExportOptions(FileName);
				Rhino.PlugIns.WriteFileResult ExportResult = DatasmithRhinoSceneExporter.Export(doc, ExportOptions);

				switch (ExportResult)
				{
					case Rhino.PlugIns.WriteFileResult.Success:
						CommandResult = Result.Success;
						break;
					case Rhino.PlugIns.WriteFileResult.Cancel:
						CommandResult = Result.Cancel;
						break;
					case Rhino.PlugIns.WriteFileResult.Failure:
					default:
						break;
				}
			}
			else if (SaveDialogResult == Eto.Forms.DialogResult.Cancel)
			{
				CommandResult = Result.Cancel;
			}

			return CommandResult;
		}
	}
}