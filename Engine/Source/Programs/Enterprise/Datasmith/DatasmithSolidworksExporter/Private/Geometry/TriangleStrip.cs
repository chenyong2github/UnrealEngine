// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Runtime.InteropServices;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;

namespace SolidworksDatasmith.Geometry
{
    [ComVisible(false)]
    public class TriangleStrip
    {
        public StripUnion vertices { get; set; }
        public StripUnion normals { get; set; }
        public StripUnion texcoords { get; set; }

        public int numStrips;
        public int numVertices;
        public int numTris;

        public int[] StripOffsets { get; set; }
        public int[] TriangleCounts { get; set; }

        public int trianglesInIndexBuffer { get; set; } = 0;

        public TriangleStrip(float[] verts, float[] norms)
        {
            vertices = new StripUnion(verts);
            normals = new StripUnion(norms);

            numStrips = vertices.NumStrips;
            if (numStrips > 0)
            {
                StripOffsets = new int[numStrips];
                TriangleCounts = new int[numStrips];

                numVertices = (vertices.Floats.Length - 1 - numStrips) / 3;

                StripOffsets[0] = numStrips + 1;
                numTris = vertices.Ints[1] - 2;
                TriangleCounts[0] = 0;

                for (int i = 1; i < numStrips; i++)
                {
                    numTris += vertices.Ints[1 + i] - 2;
                    StripOffsets[i] = StripOffsets[i - 1] + vertices.Ints[i] * 3;
                    TriangleCounts[i] = TriangleCounts[i - 1] + vertices.Ints[i] - 2;
                }
            }
        }

        public int[] BuildIndices()
        {
            int[] indices = null;
            if (numVertices >= 3)
            {
                trianglesInIndexBuffer = 0;

                indices = new int[numTris * 3];

                var stripCounts = vertices.StripCounts;
                var stripOffsets = vertices.StripOffsets;

                for (int stripindex = 0; stripindex < numStrips; stripindex++)
                {
                    int vertexCountInStrip = stripCounts[stripindex];
                    int vertexIndexForStrip = stripOffsets[stripindex];
                    if (vertexCountInStrip >= 3)
                    {
                        for (int triangleindex = 0; triangleindex < (vertexCountInStrip - 2); triangleindex++)
                        {
                            int idx = trianglesInIndexBuffer * 3;
                            trianglesInIndexBuffer++;

                            indices[idx + 0] = vertexIndexForStrip + triangleindex + 0;
                            if ((triangleindex & 1) == 0)
                            {
                                indices[idx + 1] = vertexIndexForStrip + triangleindex + 1;
                                indices[idx + 2] = vertexIndexForStrip + triangleindex + 2;
                            }
                            else
                            {
                                indices[idx + 1] = vertexIndexForStrip + triangleindex + 2;
                                indices[idx + 2] = vertexIndexForStrip + triangleindex + 1;
                            }
                        }
                    }
                }
            }
            return indices;
        }
        
        public int GetNumVerticesInStrip(int stripNum)
        {
            return vertices.Ints[1 + stripNum];
        }
    }
}
