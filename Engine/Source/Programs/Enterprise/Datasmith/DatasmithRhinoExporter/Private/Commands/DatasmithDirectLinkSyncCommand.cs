// Copyright Epic Games, Inc. All Rights Reserved.

using Rhino;
using Rhino.Commands;

namespace DatasmithRhino.Commands
{
	/**
	 * Command used to trigger a direct link scene synchronization.
	 */
	public class DatasmithDirectLinkSyncCommand : Command
	{
		public DatasmithDirectLinkSyncCommand()
		{
			// Rhino only creates one instance of each command class defined in a
			// plug-in, so it is safe to store a reference in a static property.
			Instance = this;
		}

		/**
		 * The only instance of this command.
		 */
		public static DatasmithDirectLinkSyncCommand Instance {
			get; private set;
		}

		/**
		 * The command name as it appears on the Rhino command line.
		 */
		public override string EnglishName {
			get { return "DatasmithDirectLinkSync"; }
		}

		///TODO: This needs to be localized.
		public override string LocalName {
			get { return "DatasmithDirectLinkSync"; }
		}

		protected override Result RunCommand(RhinoDoc doc, RunMode mode)
		{
			// Usually commands in export plug-ins are used to modify settings and behavior.
			// The export work itself is performed by the DatasmithRhino6 class.

			DatasmithRhinoSceneExporter.ExportDirectLink(ref DatasmithRhino6.Instance.DatasmithScene, ref DatasmithRhino6.Instance.DirectLink, doc);

			return Result.Success;
		}
	}
}