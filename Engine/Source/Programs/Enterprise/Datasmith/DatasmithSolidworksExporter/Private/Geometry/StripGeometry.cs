// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Runtime.InteropServices;
using SolidworksDatasmith.SwObjects;

namespace SolidworksDatasmith.Geometry
{
    [ComVisible(false)]
    public class StripGeometryBody
    {
        public BoundingBox Bounds { get; set; } = null;
        public List<StripGeometryFace> Faces { get; set; } = new List<StripGeometryFace>();

        public void ExtractGeometry(List<GeometryChunk> chunks, SwMaterial materialOverride)
        {
            foreach (var f in Faces)
            {
                var g = f.ExtractGeometry(this, materialOverride);
                if (g != null)
                    chunks.Add(g);
            }
        }
    }

    [ComVisible(false)]
    public class StripGeometryFace
    {
        public SwMaterial Material { get; set; } = null;
        public TriangleStrip Strip { get; set; } = null;

        public GeometryChunk ExtractGeometry(StripGeometryBody body, SwMaterial materialOverride)
        {
            var vertices = Strip?.vertices.Floats;
            if (vertices == null)
                return null;

            int stripVertexCount = Strip.numVertices;
            int[] indices = Strip.BuildIndices();
            float[] normals = Strip.normals.Floats;
            float[] texcoords = Strip.texcoords.Floats;

            int numFaces = Strip.trianglesInIndexBuffer;

            int baseOffset = Strip.StripOffsets[0];

            GeometryChunk chunk = new GeometryChunk(baseOffset, vertices, normals, texcoords, indices, body.Bounds, (materialOverride != null) ? materialOverride : Material);

            return chunk;
        }
    }
    
    [ComVisible(false)]
    public class StripGeometry
    {
        public List<StripGeometryBody> Bodies { get; set; } = new List<StripGeometryBody>();

        public List<GeometryChunk> ExtractGeometry(SwMaterial materialOverride = null)
        {
            List<GeometryChunk> chunks = new List<GeometryChunk>();
            try
            {
                foreach (var b in Bodies)
                    b.ExtractGeometry(chunks, materialOverride);
            }
            catch (Exception e)
            {
                var s = e.Message;
            }
            return chunks;
        }
    }
}
