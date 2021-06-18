// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Runtime.InteropServices;

namespace SolidworksDatasmith.Geometry
{
    [ComVisible(false)]
    [StructLayout(LayoutKind.Explicit)]
    public struct StripUnion
    {
        [FieldOffset(sizeof(ulong))]
        readonly int[] ints;

        [FieldOffset(sizeof(ulong))]
        readonly float[] floats;

        public StripUnion(float[] v) : this()
        {
            this.floats = v;
        }

        public StripUnion(int[] v) : this()
        {
            this.ints = v;
        }

        public StripUnion(int elements) : this()
        {
            ints = new int[elements];
        }

        public int[] Ints { get { return ints; } }
        public float[] Floats { get { return floats; } }
        public int NumStrips { get { return ints[0]; } }

        public int[] StripCounts
        {
            get
            {
                int[] counts = new int[NumStrips];
                for (int i = 0; i < NumStrips; i++)
                    counts[i] = ints[i + 1];
                return counts;
            }
        }

        public int[] StripOffsets
        {
            get
            {
                int[] offsets = new int[NumStrips];
                int offset = 0;
                for (int i = 0; i < NumStrips; i++)
                {
                    offsets[i] = offset;
                    offset += ints[i + 1];
                }
                return offsets;
            }
        }

        public int StartOffset { get { return NumStrips + 1; } }
    }
}
