// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Runtime.InteropServices;

namespace SolidworksDatasmith.Geometry
{
    [ComVisible(false)]
    public class StripVector
    {
        StripUnion m_base_array;
        int m_data_start_index;
        int m_data_subarrays;
        int[] m_subarray_vertex_offset;

        public int NumberOfStrips { get { return m_base_array.NumStrips; } }
        public int TotalBackingElements
        {
            get
            {
                if (m_subarray_vertex_offset == null)
                {
                    // Just a normal array backing us, no header
                    return m_base_array.Floats.Length;
                }
                else
                {
                    // We're in solidworks data mode, skip header data
                    return (m_base_array.Floats.Length - 1 - NumberOfStrips);
                }

            }
        }
        public int[] StripVertices { get { return m_base_array.StripCounts; } }
        public int[] StripVertexOffsets { get { return m_base_array.StripOffsets; } }
        public float[] BaseFloatArray { get { return m_base_array.Floats; } }
        public int[] BaseIntArray { get { return m_base_array.Ints; } }

        public StripVector()
        {
        }

        public static StripVector BuildStripVector(StripUnion union)
        {
            StripVector v = new StripVector();
            v.m_base_array = union;
            v.m_data_subarrays = v.m_base_array.NumStrips;
            v.m_subarray_vertex_offset = v.m_base_array.StripOffsets;
            v.m_data_start_index = v.m_base_array.StartOffset;
            return v;
        }

        public StripVector(int pdimensions, int pelements)
        {
            m_base_array = new StripUnion(pdimensions * pelements);
            m_data_subarrays = 0;
            m_subarray_vertex_offset = null;
            m_data_start_index = 0;
        }

        public Vec2 GetVector2(int index)
        {
            return new Vec2(m_base_array.Floats[(index << 1) + 0 + m_data_start_index],
                               m_base_array.Floats[(index << 1) + 1 + m_data_start_index]);
        }

        public Vec2 GetVector2(int strip, int index)
        {
            return GetVector2(index + m_subarray_vertex_offset[strip]);
        }

        public Vec3 GetVector3(int index)
        {
            return new Vec3(m_base_array.Floats[(index * 3) + 0 + m_data_start_index],
                               m_base_array.Floats[(index * 3) + 1 + m_data_start_index],
                               m_base_array.Floats[(index * 3) + 2 + m_data_start_index]);
        }

        public Vec3 GetVector3(int strip, int index)
        {
            return GetVector3(index + m_subarray_vertex_offset[strip]);
        }

        public void setTriangleIndices(uint ptriangleindex, Triangle pindicies)
        {
            m_base_array.Ints[(ptriangleindex * 3) + 0 + m_data_start_index] = pindicies.Index1;
            m_base_array.Ints[(ptriangleindex * 3) + 1 + m_data_start_index] = pindicies.Index2;
            m_base_array.Ints[(ptriangleindex * 3) + 2 + m_data_start_index] = pindicies.Index3;
        }
    }
}
