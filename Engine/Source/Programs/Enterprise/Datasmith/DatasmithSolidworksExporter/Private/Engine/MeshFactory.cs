// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Runtime.InteropServices;

using SolidworksDatasmith.Geometry;

namespace SolidworksDatasmith.Engine
{
    [ComVisible(false)]
    public class MeshFactory
    {
        private class Mould
        {
            public string Part { get; set; }
            public string Name { get; set; }
            public Geometry.StripGeometry StripGeom { get; set; } = null;
            public List<Instance> Instances = new List<Instance>();
        }

        private class Instance
        {
            // signature data
            public int materialID = -1;
            public bool UseDefaultMaterial = false;

            // mesh data
            public FDatasmithFacadeMesh Mesh = null;
            public FDatasmithFacadeMeshElement MeshElement = null;

            // if true, it means the mesh needs to be saved (or re-saved) to disk
            public bool Dirty = true;

            // functions
            public bool Compare(SwObjects.SwMaterial material)
            {
                if (material == null)
                {
                    return UseDefaultMaterial;
                }
                if (materialID != material.ID) return false;
                return true;
            }

            public Instance(SwObjects.SwMaterial material)
            {
                if (material == null)
                    UseDefaultMaterial = true;
                else
                {
                    materialID = material.ID;
                }
            }
        }

        private Dictionary<string, Mould> Moulds = new Dictionary<string, Mould>();
        
        public void SetMould(Processor processor, string partPath, string partName, Geometry.StripGeometry stripGeom, bool exportMesh)
        {
            Mould previous = null;
            Moulds.TryGetValue(partPath, out previous);
            if (previous != null)
                Moulds.Remove(partPath);

            Mould mould = new Mould() { Part = partPath, Name = partName, StripGeom = stripGeom };
            Moulds.Add(partPath, mould);
            
            if (previous != null)
            {
                foreach (var ii in previous.Instances)
                {
                    SwObjects.SwMaterial material = null;
                    if (ii.materialID >= 0)
                        material = processor.FindMaterial(ii.materialID);
                    FDatasmithFacadeMesh mesh;
                    FDatasmithFacadeMeshElement meshElement;
                    GetGeometryFor(processor, partPath, material, out mesh, out meshElement);
                    processor.DatasmithScene.RemoveMesh(ii.MeshElement);
                    ii.MeshElement.Dispose();
                    ii.Mesh.Dispose();
                    if (exportMesh && mesh != null && meshElement != null)
                    {
                        processor.DatasmithScene.ExportDatasmithMesh(meshElement, mesh);
                    }
                }
                previous.Instances.Clear();
                previous = null;
            }
        }

        private Mould GetMould(string partPath)
        {
            Mould res = null;
            if (Moulds.ContainsKey(partPath))
                res = Moulds[partPath];
            return res;
        }

        private List<Tuple<FDatasmithFacadeMesh, FDatasmithFacadeMeshElement>> LightWeightGeometries = new List<Tuple<FDatasmithFacadeMesh, FDatasmithFacadeMeshElement>>();

        public void GetLightweightGeometry(Processor processor, string name, float[] vertices, float[] normals, SwObjects.SwMaterial material, out FDatasmithFacadeMesh mesh, out FDatasmithFacadeMeshElement meshElement)
        {
            mesh = null;
            meshElement = null;

            int numVerts = vertices.Length / 3;
            List<GeometryChunk> chunks = new List<GeometryChunk>();
            GeometryChunk chunk = new GeometryChunk();
            chunk.Vertices = new Vec3[numVerts];
            chunk.Normals = new Vec3[numVerts];
            for (int i = 0; i < numVerts; i++)
            {
                int idx = i * 3;
                chunk.Vertices[i] = new Vec3(vertices[idx], vertices[idx + 1], vertices[idx + 2]);
                chunk.Normals[i] = new Vec3(normals[idx], normals[idx + 1], normals[idx + 2]);
            }

            int matID = -1;
            if (material != null)
                matID = material.ID;

            int numTriangles = numVerts / 3;
            chunk.Indices = new Triangle[numTriangles];
            for (int i = 0; i < numTriangles; i++)
            {
                int vi = i * 3;
                chunk.Indices[i] = new Triangle(vi, vi + 1, vi + 2, matID);
            }
            chunks.Add(chunk);
            Geometry.Geometry geom = null;
            try
            {
                geom = processor.ProcessChunks(chunks);
            }
            catch (Exception e)
            {
                var s = e.Message;
            }
            if (geom != null)
            {
                processor.CreateMesh(name, geom.Vertices, geom.Normals, geom.TexCoords, geom.Indices, out mesh, out meshElement);
                LightWeightGeometries.Add(new Tuple<FDatasmithFacadeMesh, FDatasmithFacadeMeshElement>(mesh, meshElement));
            }
        }

        public void GetGeometryFor(Processor processor, string partPath, SwObjects.SwMaterial material, out FDatasmithFacadeMesh mesh, out FDatasmithFacadeMeshElement meshElement)
        {
            mesh = null;
            meshElement = null;

            var mould = GetMould(partPath);
            if (mould != null)
            {
                foreach (var i in mould.Instances)
                {
                    if (i.Compare(material))
                    {
                        mesh = i.Mesh;
                        meshElement = i.MeshElement;
                        return;
                    }
                }

                // If we get here, no instance was found. Create one.
                Instance inst = new Instance(material);
                Geometry.Geometry geom = null;
                try
                {
                    var chunks = mould.StripGeom.ExtractGeometry(material);
                    if (chunks != null && chunks.Count > 0)
                        geom = processor.ProcessChunks(chunks);
                }
                catch (Exception e)
                {
                    var s = e.Message;
                }
                if (geom != null)
                {
                    string name = mould.Name + ((mould.Instances.Count > 0) ? ("_" + mould.Instances.Count) : "");
                    processor.CreateMesh(name, geom.Vertices, geom.Normals, geom.TexCoords, geom.Indices, out inst.Mesh, out inst.MeshElement);
                }
                mould.Instances.Add(inst);

                mesh = inst.Mesh;
                meshElement = inst.MeshElement;
            }
        }

        public void ExportMeshes(Processor processor, bool bForce)
        {
            var moulds = Moulds.ToArray();
            Parallel.For(0, moulds.Length, mm =>
            {
                var m = moulds[mm];
                foreach (var i in m.Value.Instances)
                {
                    if (i.Dirty || bForce)
                    {
                        processor.DatasmithScene.ExportDatasmithMesh(i.MeshElement, i.Mesh);
                        i.Dirty = false;
                    }
                }
            });
            Parallel.For(0, LightWeightGeometries.Count, i =>
            {
                var ll = LightWeightGeometries[i];
                processor.DatasmithScene.ExportDatasmithMesh(ll.Item2, ll.Item1);
            });
        }

        public FDatasmithFacadeMeshElement GetFacadeElement(string meshname)
        {
            foreach (var item in Moulds)
            {
                if (item.Value.Name == meshname && item.Value.Instances.Count != 0)
                {
                    return item.Value.Instances[0].MeshElement;
                }
            }
            return null;
        }
    }
}
