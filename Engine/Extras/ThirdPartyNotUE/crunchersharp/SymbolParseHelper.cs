using System.Data;

namespace CruncherSharp
{
	/** Helper definition for parsing data from the PDB file */
	public static class LocationType
	{
		public static uint IsNull		= 0;
		public static uint IsThisRel	= 4;
		public static uint IsBitField	= 6;
	}

	/// <summary>
	/// Util class with helper functions for the creation of the view model
	/// </summary>
	public static class Utils
	{

		/// <summary>
		///Create the data table that displays on the left hand side of the screen
		///where each symbol name will be displayed, along with it's summary.
		/// </summary>
		/// <returns>System.Data.DataTable with the proper columns</returns>
		public static DataTable CreateDataTable()
		{
			DataTable table = new DataTable("Symbols");

			table.Columns.Add(
				new DataColumn("Symbol") { ReadOnly = true }
			);

			table.Columns.Add(
				new DataColumn("Size", System.Type.GetType("System.Int32")) { ReadOnly = true }
			);

			table.Columns.Add(
				new DataColumn("Padding", System.Type.GetType("System.Int32")) { ReadOnly = true }
			);

			table.Columns.Add(
				new DataColumn("Padding/Size", System.Type.GetType("System.Double")) { ReadOnly = true }
			);

			return table;
		}
	}	
}
