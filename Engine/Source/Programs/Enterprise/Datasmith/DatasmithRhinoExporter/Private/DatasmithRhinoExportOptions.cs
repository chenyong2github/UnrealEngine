// Copyright Epic Games, Inc. All Rights Reserved.
using Rhino.FileIO;
using Rhino.Geometry;

namespace DatasmithRhino
{
	public class FDatasmithRhinoExportOptions
	{
		public bool WriteSelectedObjectsOnly { get; private set; } = false;
		public Transform Xform { get; private set; } = Transform.Identity;
		public string DestinationFileName { get; private set; }

		public FDatasmithRhinoExportOptions(string TargetFilePath)
		{
			DestinationFileName = TargetFilePath;
		}

		public FDatasmithRhinoExportOptions(FileWriteOptions RhinoFileWriteOptions, string TargetFilePath)
			: this(TargetFilePath)
		{
			WriteSelectedObjectsOnly = RhinoFileWriteOptions.WriteSelectedObjectsOnly;
			Xform = RhinoFileWriteOptions.Xform;
		}
	}
}
