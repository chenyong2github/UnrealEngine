// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.IO;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// CLion project file generator which is just the CMakefileGenerator and only here for UBT to match against
	/// </summary>
	class CLionGenerator : CMakefileGenerator
	{
		/// <summary>
		/// Creates a new instance of the <see cref="CMakefileGenerator"/> class.
		/// </summary>
		public CLionGenerator(FileReference InOnlyGameProject)
			: base(InOnlyGameProject)
		{
		}

		/// <summary>
		/// Writes a file that CLion uses to know what directories to exclude from indexing. This should speed up indexing
		/// </summary>
		protected void WriteCLionIgnoreDirs()
		{
			string CLionIngoreXml =
				"<?xml version=\"1.0\" encoding=\"UTF-8\"?>" + Environment.NewLine +
				"<project version=\"4\">" + Environment.NewLine +
				  "\t<component name=\"CMakeWorkspace\" PROJECT_DIR=\"$PROJECT_DIR$\" />" + Environment.NewLine +
				  "\t<component name=\"CidrRootsConfiguration\">" + Environment.NewLine +
					"\t\t<excludeRoots>" + Environment.NewLine +
					  "\t\t\t<file path=\"$PROJECT_DIR$/Binaries\" />" + Environment.NewLine +
					  "\t\t\t<file path=\"$PROJECT_DIR$/Build\" />" + Environment.NewLine +
					  "\t\t\t<file path=\"$PROJECT_DIR$/Content\" />" + Environment.NewLine +
					  "\t\t\t<file path=\"$PROJECT_DIR$/DataTables\" />" + Environment.NewLine +
					  "\t\t\t<file path=\"$PROJECT_DIR$/DerivedDataCache\" />" + Environment.NewLine +
					  "\t\t\t<file path=\"$PROJECT_DIR$/FeaturePacks\" />" + Environment.NewLine +
					  "\t\t\t<file path=\"$PROJECT_DIR$/Intermediate\" />" + Environment.NewLine +
					  "\t\t\t<file path=\"$PROJECT_DIR$/LocalBuilds\" />" + Environment.NewLine +
					  "\t\t\t<file path=\"$PROJECT_DIR$/Samples\" />" + Environment.NewLine +
					  "\t\t\t<file path=\"$PROJECT_DIR$/Saved\" />" + Environment.NewLine +
					  "\t\t\t<file path=\"$PROJECT_DIR$/Templates\" />" + Environment.NewLine +
					  "" + Environment.NewLine +
					  "\t\t\t<file path=\"$PROJECT_DIR$/Engine/Binaries\" />" + Environment.NewLine +
					  "\t\t\t<file path=\"$PROJECT_DIR$/Engine/Build\" />" + Environment.NewLine +
					  "\t\t\t<file path=\"$PROJECT_DIR$/Engine/Content\" />" + Environment.NewLine +
					  "\t\t\t<file path=\"$PROJECT_DIR$/Engine/DerivedDataCache\" />" + Environment.NewLine +
					  "\t\t\t<file path=\"$PROJECT_DIR$/Engine/Documentation\" />" + Environment.NewLine +
					  "\t\t\t<file path=\"$PROJECT_DIR$/Engine/Extras\" />" + Environment.NewLine +
					  "\t\t\t<file path=\"$PROJECT_DIR$/Engine/Intermediate\" />" + Environment.NewLine +
					  "\t\t\t<file path=\"$PROJECT_DIR$/Engine/Programs\" />" + Environment.NewLine +
					  "\t\t\t<file path=\"$PROJECT_DIR$/Engine/Saved\" />" + Environment.NewLine +
					"\t\t</excludeRoots>" + Environment.NewLine +
				  "\t</component>" + Environment.NewLine +
				"</project>" + Environment.NewLine;

			string FullFileName = Path.Combine(MasterProjectPath.FullName, ".idea/misc.xml");
			WriteFileIfChanged(FullFileName, CLionIngoreXml, new UTF8Encoding());
		}

        protected override bool WriteProjectFiles()
        {
            WriteCLionIgnoreDirs();
            return base.WriteProjectFiles();
        }
	}
}
