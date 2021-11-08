// Copyright Epic Games, Inc. All Rights Reserved.

using System.Runtime.InteropServices;

namespace DatasmithSolidworks
{
	[ComVisible(false)]
	public class FTriangle
	{
		private int[] Indices = new int[3];

		public int Index1 { get { return Indices[0]; } set { Indices[0] = value; } }
		public int Index2 { get { return Indices[1]; } set { Indices[1] = value; } }
		public int Index3 { get { return Indices[2]; } set { Indices[2] = value; } }

		public int this[int InWhich]
		{
			get { return Indices[InWhich]; }
			set { Indices[InWhich] = value; }
		}

		public int MaterialID { get; set; }

		public FTriangle(int InIdx0, int InIdx1, int InIdx2, int InMaterialID)
		{
			Indices[0] = InIdx0;
			Indices[1] = InIdx1;
			Indices[2] = InIdx2;
			MaterialID = InMaterialID;
		}

		public FTriangle Offset(int InOffset)
		{
			return new FTriangle(Indices[0] + InOffset, Indices[1] + InOffset, Indices[2] + InOffset, MaterialID);
		}

		public override string ToString()
		{
			return "" + Index1 + "," + Index2 + "," + Index3;
		}
	}
}
