// Copyright Epic Games, Inc. All Rights Reserved.

using DatasmithRhino.DirectLink;
using DatasmithRhino.Properties.Localization;

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

		public override string LocalName {
			get { return Resources.DatasmithDirectLinkConnectionsCommand; }
		}

		protected override Result RunCommand(RhinoDoc RhinoDocument, RunMode Mode)
		{
			DatasmithRhinoDirectLinkManager DirectLinkManager = DatasmithRhinoPlugin.Instance?.DirectLinkManager;

			bool bSuccess = false;
			if (DirectLinkManager != null)
			{
				bSuccess = DirectLinkManager.OpenConnectionManangementWindow();
			}

			return bSuccess ? Result.Success : Result.Failure;
		}
	}
}