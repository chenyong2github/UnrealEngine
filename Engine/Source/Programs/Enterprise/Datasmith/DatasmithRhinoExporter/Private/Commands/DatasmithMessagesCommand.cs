// Copyright Epic Games, Inc. All Rights Reserved.

using Rhino;
using Rhino.Commands;

namespace DatasmithRhino.Commands
{
	/**
	 * Command used to open the datasmith message windows.
	 */
	public class DatasmithMessagesCommand : Command
	{
		public DatasmithMessagesCommand()
		{
			// Rhino only creates one instance of each command class defined in a
			// plug-in, so it is safe to store a reference in a static property.
			Instance = this;
		}

		/**
		 * The only instance of this command.
		 */
		public static DatasmithMessagesCommand Instance {
			get; private set;
		}

		/**
		 * The command name as it appears on the Rhino command line.
		 */
		public override string EnglishName {
			get { return "DatasmithMessages"; }
		}

		//TODO: This needs to be localized.
		public override string LocalName {
			get { return "DatasmithMessages"; }
		}

		protected override Result RunCommand(RhinoDoc doc, RunMode mode)
		{
			/*
			 * TODO: There is currently no message window we can show.
			 * We either need to implement a message/logging window in Slate and expose it with the DatasmithFacade or create an Eto.Form for this.
			 */
			return Result.Success;
		}
	}
}