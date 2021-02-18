// Copyright Epic Games, Inc. All Rights Reserved.

using DatasmithRhino.DirectLink;
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

		protected override Result RunCommand(RhinoDoc RhinoDocument, RunMode Mode)
		{
			DatasmithRhinoDirectLinkManager DirectLinkManager = DatasmithRhinoPlugin.Instance?.DirectLinkManager;

			if (DirectLinkManager != null)
			{
				return DirectLinkManager.Synchronize(RhinoDocument);
			}

			return Result.Failure;
		}
	}
}