using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace CruncherSharp
{
	/** Helper definition for parsing data from the PDB file */
	public static class LocationType
	{
		public static uint IsNull = 0;
		public static uint IsThisRel = 4;
		public static uint IsBitField = 6;
	}
}
