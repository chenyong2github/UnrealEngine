// Copyright Epic Games, Inc. All Rights Reserved.

using Rhino;
using Rhino.Commands;

namespace DatasmithRhino.Commands
{
	/**
	 * Command used to open the direct link connection management window.
	 */
	public class DatasmithDirectLinkConnectionsCommand : Command
	{
		public DatasmithDirectLinkConnectionsCommand()
		{
			// Rhino only creates one instance of each command class defined in a
			// plug-in, so it is safe to store a reference in a static property.
			Instance = this;
		}

		/**
		 * The only instance of this command.
		 */
		public static DatasmithDirectLinkConnectionsCommand Instance {
			get; private set;
		}

		/**
		 * The command name as it appears on the Rhino command line.
		 */
		public override string EnglishName {
			get { return "DatasmithDirectLinkConnections"; }
		}

		///TODO: This needs to be localized.
		public override string LocalName {
			get { return "DatasmithDirectLinkConnections"; }
		}

		protected override Result RunCommand(RhinoDoc doc, RunMode mode)
		{
			// Usually commands in export plug-ins are used to modify settings and behavior.
			// The export work itself is performed by the DatasmithRhino6 class.
			IDirectLinkUI DirectLinkUI = IDatasmithExporterUIModule.Get()?.GetDirectLinkExporterUI();
			DirectLinkUI?.OpenDirectLinkStreamWindow();

			return Result.Success;
		}
	}
}