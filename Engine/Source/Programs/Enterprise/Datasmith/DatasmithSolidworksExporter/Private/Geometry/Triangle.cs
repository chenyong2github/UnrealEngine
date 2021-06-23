// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Runtime.InteropServices;

namespace SolidworksDatasmith.Geometry
{
    [ComVisible(false)]
    public class Triangle
    {
        private int[] indices = new int[3];
        public int Index1 { get { return indices[0]; } set { indices[0] = value; } }
        public int Index2 { get { return indices[1]; } set { indices[1] = value; } }
        public int Index3 { get { return indices[2]; } set { indices[2] = value; } }
        public int this[int which] { get { return indices[which]; } set { indices[which] = value; } }
        public int MaterialID { get; set; }

        public Triangle(int i0, int i1, int i2, int materialID)
        {
            indices[0] = i0;
            indices[1] = i1;
            indices[2] = i2;
            MaterialID = materialID;
        }

        public Triangle Offset(int offset)
        {
            return new Triangle(indices[0] + offset, indices[1] + offset, indices[2] + offset, MaterialID);
        }

        public override string ToString()
        {
            return "" + Index1 + "," + Index2 + "," + Index3;
        }
    }
}
