// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using Rhino;
using Rhino.Commands;
using Rhino.Input;
using Rhino.Input.Custom;

namespace DatasmithRhino6
{
	public class DatasmithRhino6Command : Command
	{
		public DatasmithRhino6Command()
		{
			// Rhino only creates one instance of each command class defined in a
			// plug-in, so it is safe to store a reference in a static property.
			Instance = this;
		}

		///<summary>The only instance of this command.</summary>
		public static DatasmithRhino6Command Instance {
			get; private set;
		}

		///<returns>The command name as it appears on the Rhino command line.</returns>
		public override string EnglishName {
			get { return "Datasmith"; }
		}

		protected override Result RunCommand(RhinoDoc doc, RunMode mode)
		{
			// Usually commands in export plug-ins are used to modify settings and behavior.
			// The export work itself is performed by the DatasmithRhino6 class.

			return Result.Success;
		}
	}
}
