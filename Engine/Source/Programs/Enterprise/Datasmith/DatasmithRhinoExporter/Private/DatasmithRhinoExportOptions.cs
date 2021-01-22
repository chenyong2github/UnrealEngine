// Copyright Epic Games, Inc. All Rights Reserved.
using Rhino;
using Rhino.FileIO;
using Rhino.Geometry;

namespace DatasmithRhino
{
	public class DatasmithRhinoExportOptions
	{
		public const string UntitledSceneName = "";

		public bool WriteSelectedObjectsOnly { get; private set; } = false;
		public Transform Xform { get; private set; } = Transform.Identity;
		public RhinoDoc RhinoDocument { get; private set; }
		public FDatasmithFacadeScene DatasmithScene { get; private set; }

		public DatasmithRhinoExportOptions(RhinoDoc InRhinoDocument, FDatasmithFacadeScene InDatasmithScene)
		{
			RhinoDocument = InRhinoDocument;
			DatasmithScene = InDatasmithScene;
		}

		public DatasmithRhinoExportOptions(FileWriteOptions RhinoFileWriteOptions, RhinoDoc InRhinoDocument, FDatasmithFacadeScene InDatasmithScene)
			: this(InRhinoDocument, InDatasmithScene)
		{
			WriteSelectedObjectsOnly = RhinoFileWriteOptions.WriteSelectedObjectsOnly;
			Xform = RhinoFileWriteOptions.Xform;
		}
	}
}
