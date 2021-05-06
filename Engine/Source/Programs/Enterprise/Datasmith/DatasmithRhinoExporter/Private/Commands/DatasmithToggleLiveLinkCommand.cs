// Copyright Epic Games, Inc. All Rights Reserved.

using DatasmithRhino.DirectLink;
using Rhino;
using Rhino.Commands;

#if DATASMITHRHINO_EXPERIMENTAL
namespace DatasmithRhino.Commands
{
	/**
	 * Command used to toggle a direct link livelink scene synchronization on and off.
	 */
	public class DatasmithToggleLiveLinkCommand : Command
	{
		public DatasmithToggleLiveLinkCommand()
		{
			// Rhino only creates one instance of each command class defined in a
			// plug-in, so it is safe to store a reference in a static property.
			Instance = this;
		}

		/**
		 * The only instance of this command.
		 */
		public static DatasmithToggleLiveLinkCommand Instance {
			get; private set;
		}

		/**
		 * The command name as it appears on the Rhino command line.
		 */
		public override string EnglishName {
			get { return "DatasmithToggleLiveLink"; }
		}

		///TODO: This needs to be localized.
		public override string LocalName {
			get { return "DatasmithToggleLiveLink"; }
		}

		protected override Result RunCommand(RhinoDoc RhinoDocument, RunMode Mode)
		{
			DatasmithRhinoDirectLinkManager DirectLinkManager = DatasmithRhinoPlugin.Instance?.DirectLinkManager;
			Result CommandResult = Result.Failure;

			if (DirectLinkManager != null)
			{
				bool bLiveLinkToggledValue = !DirectLinkManager.bLiveLinkActive;
				CommandResult = DirectLinkManager.SetLiveLink(bLiveLinkToggledValue);

				if (CommandResult == Result.Success)
				{
					RhinoApp.WriteLine(string.Format("Datasmith LiveLink {0}.", bLiveLinkToggledValue ? "enabled" : "disabled"));
				}
			}

			return CommandResult;
		}
	}
}
#endif //DATASMITHRHINO_EXPERIMENTAL