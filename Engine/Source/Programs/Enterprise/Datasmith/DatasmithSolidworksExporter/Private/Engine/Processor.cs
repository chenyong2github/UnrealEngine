// Copyright Epic Games, Inc. All Rights Reserved.

//todo: get rid of this later
#define DATASMITH_VARIANTS

using System;
using System.IO;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Runtime.InteropServices;
using System.Security.Permissions;
using SolidworksDatasmith.Geometry;
using SolidworksDatasmith.SwObjects;

/*
    Material:
        passed raw material data, builds a datasmith material (and textures if any)

    Part:
        passed raw solidworks data. Decodes raw solidworks data into triangles, UVs, vertices etc.. builds a datasmith mesh and mesh actor
        associates the mesh to a known material

    Component:
        passed raw solidworks data (name, transform, parent, children). builds a datasmith actor and connects to parent, or updates existing tree
        if the relationships have changed

    Component Material:
        informs the engine about a component wanting to override its sub-tree instances with a certain material
*/

namespace SolidworksDatasmith.Engine
{
    [ComVisible(false)]
    public class Processor
    {
        private Thread processorThread = null;
        private object _lock = new object();
        private bool _running = false;
        public bool Running
        {
            get
            {
                bool state = false;
                lock (_lock)
                {
                    state = _running;
                }
                return state;
            }

            set
            {
                lock (_lock)
                {
                    _running = value;
                }
            }
        }

        public CommandQueue Queue = new CommandQueue();

        // processor data
        private FDatasmithFacadeScene _datasmithScene = null;
        public FDatasmithFacadeScene DatasmithScene { get { return _datasmithScene; } }
        private FDatasmithFacadeDirectLink DatasmithDirectLink = null;
        private FDatasmithFacadeUEPbrMaterial DefaultMaterial;
        private static readonly string DefaultMaterialName = "Default";
        private Dictionary<string, FDatasmithFacadeTexture> DatasmithTextures = new Dictionary<string, FDatasmithFacadeTexture>();
        private Dictionary<SwMaterial, FDatasmithFacadeUEPbrMaterial> SwMat2Datasmith = new Dictionary<SwMaterial, FDatasmithFacadeUEPbrMaterial>();
        private Dictionary<string, SwMaterial> ComponentMaterials = new Dictionary<string, SwMaterial>();
        private MeshFactory MeshFactory = new MeshFactory();
        private HashSet<string> MeshNames = new HashSet<string>();
        private List<Tuple<SwLightweightMaterial, SwMaterial>> LightweightMaterials = new List<Tuple<SwLightweightMaterial, SwMaterial>>();
        private Dictionary<int, SwMaterial> SwIDToMat = new Dictionary<int, SwMaterial>();
		private Dictionary<SwCamera, FDatasmithFacadeActorCamera> SwCamera2Datasmith = new Dictionary<SwCamera, FDatasmithFacadeActorCamera>();
        private SwScene Scene;

        private class ActorData
        {
            public FDatasmithFacadeActor Actor { get; set; } = null;
            public FDatasmithFacadeActor ParentActor { get; set; } = null;
            public FDatasmithFacadeActorMesh MeshActor { get; set; } = null;
        }
        private Dictionary<string, ActorData> Component2DatasmithActor = new Dictionary<string, ActorData>();

        public Processor(SwScene scene)
        {
            Scene = scene;
        }

        public void Start()
        {
            processorThread = new Thread(() => ProcessorThread(this));
            processorThread.Start();
        }

        [SecurityPermissionAttribute(SecurityAction.Demand, ControlThread = true)]
        private void KillRenderingThread()
        {
            processorThread.Abort();
        }

        // checks whether we already have an identical one
        private SwMaterial GetLightweightMaterial(SwLightweightMaterial mat)
        {
            foreach (var matIn in LightweightMaterials)
            {
                if (SwLightweightMaterial.AreTheSame(matIn.Item1, mat))
                    return matIn.Item2;
            }
            mat.ID = 0x000DEAD + LightweightMaterials.Count;
            SwMaterial res = mat.ToSwMaterial();
            LightweightMaterials.Add(new Tuple<SwLightweightMaterial, SwMaterial>(mat, res));
            var dm = MaterialToDatasmith(res);
            SwMat2Datasmith.Add(res, dm);
            SwIDToMat.Add(mat.ID, res);
            return res;
        }

        private FDatasmithFacadeUEPbrMaterial GetDatasmithMaterial(SwMaterial mat, bool addIfNotExisting)
        {
            FDatasmithFacadeUEPbrMaterial dm = null;
            foreach (var mm in SwMat2Datasmith)
            {
                if (SwMaterial.AreTheSame(mm.Key, mat, false))
				{
					if (!SwIDToMat.ContainsKey(mat.ID))
					{
						SwIDToMat.Add(mat.ID, mm.Key);
					}
				    return mm.Value;
				}
            }
            if (addIfNotExisting)
            {
                dm = MaterialToDatasmith(mat);
                SwMat2Datasmith.Add(mat, dm);
            }
            return dm;
        }

        public void Stop()
        {
			if (Running)
			{
				Running = false;
				// Send an empty command to the event queue to wake up the thread
				AddCommand(null);
				if (!processorThread.Join(2000))
					KillRenderingThread();

				DatasmithDirectLink = null;
			}
        }

        private FDatasmithFacadeActorCamera GetDatasmithCamera(SwCamera camera, out bool wasExisting)
        {
            wasExisting = false;
            foreach (var cc in SwCamera2Datasmith)
            {
                if (SwCamera.AreSame(camera, cc.Key))
                {
                    wasExisting = true;
                    return cc.Value;
                }
            }
            var dsCamera = camera.ToDatasmith();
            SwCamera2Datasmith.Add(camera, dsCamera);
            return dsCamera;
        }

        static Matrix4 AdjustTransform(float[] xform)
        {
            Matrix4 rot = Matrix4.FromRotationX(-90f);
            if (xform != null)
            {
                Matrix4 mat = new Matrix4(xform);
                mat = mat * rot;
                return mat;
            }
            return rot;
        }

        static void ProcessorThread(Processor processor)
        {
            processor.Running = true;

            string directLinkPath = Path.Combine(Path.GetTempPath(), "sw_dl_" + Guid.NewGuid().ToString());
            if (!Directory.Exists(directLinkPath))
                Directory.CreateDirectory(directLinkPath);

            // datasmith scene setup
            processor._datasmithScene = new FDatasmithFacadeScene("Solidworks", "Solidworks", "Solidworks", "2021");
            processor._datasmithScene.SetOutputPath(directLinkPath);
            processor._datasmithScene.SetName("SolidWorks_Scene");
            processor.DatasmithScene.PreExport();
            FDatasmithFacadeElement.SetCoordinateSystemType(FDatasmithFacadeElement.ECoordinateSystemType.RightHandedZup);

            processor.DatasmithDirectLink = new FDatasmithFacadeDirectLink();
            if (!processor.DatasmithDirectLink.InitializeForScene(processor.DatasmithScene))
            {
                throw new Exception("DirectLink: failed to initialize");
            }

            // default material setup
            processor.DefaultMaterial = new FDatasmithFacadeUEPbrMaterial(DefaultMaterialName);
            var colorExpr = processor.DefaultMaterial.AddMaterialExpressionColor();
            colorExpr.SetColor(0.8f, 0.8f, 0.8f, 1f);
            colorExpr.SetName("Diffuse Color");
            colorExpr.ConnectExpression(processor.DefaultMaterial.GetBaseColor());
            FDatasmithFacadeMaterialExpressionScalar roughExpr = processor.DefaultMaterial.AddMaterialExpressionScalar();
            roughExpr.SetName("Roughness");
            roughExpr.SetScalar(0.5f);
            roughExpr.ConnectExpression(processor.DefaultMaterial.GetRoughness());
            processor.DatasmithScene.AddMaterial(processor.DefaultMaterial);

            ulong idleCount = 0;

            while (true)
            {
                if (!processor.Running)
                    break;

                var command = processor.Queue.Pop();

                if (command != null)
                {
                    idleCount = 0;

                    switch (command.Type)
                    {
                        case CommandType.LIVEUPDATE:
                            {
                            	processor.MeshFactory.ExportMeshes(processor, false);
                            	processor.DatasmithDirectLink.UpdateScene(processor.DatasmithScene);
                            }
                            break;

                        case CommandType.UPDATE_PART:
                            {
                                var cmd = command as PartCommand;
                                SwSingleton.FireProgressEvent("Loading part " + cmd.Name);
                                processor.MeshFactory.SetMould(processor, cmd.PathName, cmd.Name, cmd.StripGeom, processor.Scene.bDirectLinkAutoSync);
                            }
                            break;

                        case CommandType.UPDATE_MATERIAL:
                            {
                                var cmd = command as MaterialCommand;
                                SwSingleton.FireProgressEvent("Processing Material " + cmd.Material.Name);
                                var dm = processor.GetDatasmithMaterial(cmd.Material, true);
                            }
                            break;

                        case CommandType.COMPONENT_MATERIAL:
                            {
                                var cmd = command as ComponentMaterialCommand;
                                if (processor.ComponentMaterials.ContainsKey(cmd.ComponentName))
                                    processor.ComponentMaterials[cmd.ComponentName] = cmd.Material;
                                else
                                    processor.ComponentMaterials.Add(cmd.ComponentName, cmd.Material);
                            }
                            break;

                        case CommandType.UPDATE_COMPONENT:
                            {
                                var cmd = command as ComponentCommand;
                                SwSingleton.FireProgressEvent("Processing Component" + cmd.Name);
                                if (!processor.Component2DatasmithActor.ContainsKey(cmd.Name))
                                {
                                    var data = new ActorData();
                                    FDatasmithFacadeMesh facadeMesh = null;
                                    FDatasmithFacadeMeshElement facadeMeshElement = null;

                                    if (!string.IsNullOrEmpty(cmd.PartName))
                                    {
                                        SwMaterial ComponentMaterial = null;
                                        processor.ComponentMaterials.TryGetValue(cmd.Name, out ComponentMaterial);

                                        processor.MeshFactory.GetGeometryFor(processor, cmd.PartPath, ComponentMaterial, out facadeMesh, out facadeMeshElement);

                                        if (processor.Scene.bDirectLinkAutoSync && facadeMesh != null && facadeMeshElement != null)
                                        {
                                            processor.DatasmithScene.ExportDatasmithMesh(facadeMeshElement, facadeMesh);
                                        }
                                    }

                                    if (!string.IsNullOrEmpty(cmd.ParentName))
                                    {
                                        if (processor.Component2DatasmithActor.ContainsKey(cmd.ParentName))
                                        {
                                            data.ParentActor = processor.Component2DatasmithActor[cmd.ParentName].Actor;
                                        }
                                    }

                                    data.Actor = processor.CreateInstance(cmd.Name, cmd.Label, facadeMesh, facadeMeshElement, data.ParentActor);

                                    if (cmd.Visible == false)
                                    {
                                        //no effect at this time in UE Editor. See open branch dev/UE4SW-104_Display_states
                                        //and Ticket comment at [UE4SW-104]
                                        data.Actor.SetVisibility(false);
                                    }

                                    Matrix4 rot = AdjustTransform(cmd.Transform);
                                    data.Actor.SetWorldTransform(rot);

                                    processor.Component2DatasmithActor.Add(cmd.Name, data);
                                }
                                else
                                {
                                    if (cmd.Transform != null)
                                    {
                                        var data = processor.Component2DatasmithActor[cmd.Name];
                                        Matrix4 rot = AdjustTransform(cmd.Transform);
                                        data.Actor.SetWorldTransform(rot);
                                    }
                                }
                            }
                            break;


                        case CommandType.DELETE_COMPONENT:
                            {
                                var cmd = command as DeleteComponentCommand;
                                ActorData data = null;
                                processor.Component2DatasmithActor.TryGetValue(cmd.Name, out data);
                                if (data != null)
                                {
                                    processor.DatasmithScene.RemoveActor(data.Actor, FDatasmithFacadeScene.EActorRemovalRule.RemoveChildren);
                                    processor.Component2DatasmithActor.Remove(cmd.Name);
                                }
                            }
                            break;

                        case CommandType.UPDATE_COMPONENT_TRANSFORM_MULTI:
                            {
                                var cmd = command as ComponentTransformMultiCommand;
                                foreach (var tt in cmd.Transforms)
                                {
                                    var data = processor.Component2DatasmithActor[tt.Item1];
                                    Matrix4 rot = AdjustTransform(tt.Item2);
                                    data.Actor.SetWorldTransform(rot);
                                }
                            }
                            break;

                        case CommandType.CONFIGURATION_DATA:
                            {
                                SwSingleton.FireProgressEvent("Processing Configuration Data");

                                var cmd = command as ConfigurationDataCommand;

                                // Remove any existing Datasmith LevelVariantSets as we'll re-add data
                                while (processor.DatasmithScene.GetLevelVariantSetsCount() > 0)
                                {
									processor.DatasmithScene.RemoveLevelVariantSets(processor.DatasmithScene.GetLevelVariantSets(0));
                                }
                                // Add the configurations
                                foreach (ConfigurationDataCommand.Configuration cfg in cmd.Configurations)
                                {
                                    processor.ConfigurationToDatasmith(cmd.ConfigurationsSetName, cfg);
                                }
                            }
                            break;

                        case CommandType.EXPORT_TO_FILE:
                            {
                                if (processor.Queue.Count == 0)
                                {
                                    SwSingleton.FireProgressEvent("Exporting...");
                                    var cmd = command as ExportCommand;

                                    string filePath = Path.Combine(cmd.Path, cmd.SceneName);

                                    processor.DatasmithScene.SetOutputPath(cmd.Path);
                                    processor.DatasmithScene.SetName(cmd.SceneName);

                                    processor.MeshFactory.ExportMeshes(processor, true);

                                    processor.DatasmithScene.ExportScene(filePath);

                                    processor.DatasmithScene.SetOutputPath(directLinkPath);
                                }
                                else
								{
                                    processor.AddCommand(command); // just put it back, execute only when nothing else is in the queue
								}
                            }
                            break;

                        case CommandType.LIGHTWEIGHT_COMPONENT:
                            {
                                var cmd = command as LightweightComponentCommand;
                                SwSingleton.FireProgressEvent("Processing Component" + cmd.Name);
                                if (!processor.Component2DatasmithActor.ContainsKey(cmd.Name))
                                {
                                    var data = new ActorData();
                                    FDatasmithFacadeMesh facadeMesh = null;
                                    FDatasmithFacadeMeshElement facadeMeshElement = null;

                                    if (cmd.Vertices != null && cmd.Vertices.Length > 0)
                                    {
                                        SwMaterial ComponentMaterial = null;
                                        processor.ComponentMaterials.TryGetValue(cmd.Name, out ComponentMaterial);

                                        if (ComponentMaterial == null && cmd.Material != null)
                                            ComponentMaterial = processor.GetLightweightMaterial(cmd.Material);

                                        processor.MeshFactory.GetLightweightGeometry(processor, cmd.Name, cmd.Vertices, cmd.Normals, ComponentMaterial, out facadeMesh, out facadeMeshElement);

                                        if (facadeMesh != null && facadeMeshElement != null)
                                        {
                                            processor.DatasmithScene.ExportDatasmithMesh(facadeMeshElement, facadeMesh);
                                        }
                                    }

                                    if (!string.IsNullOrEmpty(cmd.ParentName))
                                    {
                                        if (processor.Component2DatasmithActor.ContainsKey(cmd.ParentName))
                                        {
                                            data.ParentActor = processor.Component2DatasmithActor[cmd.ParentName].Actor;
                                        }
                                    }

                                    data.Actor = processor.CreateInstance(cmd.Name, cmd.Label, facadeMesh, facadeMeshElement, data.ParentActor);

                                    if (cmd.Visible == false)
                                    {
                                        //no effect at this time in UE Editor. See open branch dev/UE4SW-104_Display_states
                                        //and Ticket comment at [UE4SW-104]
                                        data.Actor.SetVisibility(false);
                                    }

                                    Matrix4 rot = AdjustTransform(cmd.Transform);
                                    data.Actor.SetWorldTransform(rot);

                                    processor.Component2DatasmithActor.Add(cmd.Name, data);
                                }
                                else
                                {
                                    if (cmd.Transform != null)
                                    {
                                        var data = processor.Component2DatasmithActor[cmd.Name];
                                        Matrix4 rot = AdjustTransform(cmd.Transform);
                                        data.Actor.SetWorldTransform(rot);
                                    }
                                }
                            }
                            break;

                        case CommandType.ADD_METADATA:
						{
							var cmd = command as MetadataCommand;
							SwSingleton.FireProgressEvent("Adding Metadata for " + cmd.MetadataOwnerName);
							FDatasmithFacadeElement element = null;
							if (cmd.MDataType == MetadataCommand.MetadataType.Actor)
							{
								if (processor.Component2DatasmithActor.ContainsKey(cmd.MetadataOwnerName))
								{
									element = processor.Component2DatasmithActor[cmd.MetadataOwnerName].Actor;
								}
							}
							else if (cmd.MDataType == MetadataCommand.MetadataType.MeshActor)
							{
								element = processor.MeshFactory.GetFacadeElement(cmd.MetadataOwnerName);
							}

							if (element != null)
							{
								FDatasmithFacadeMetaData MetaData = processor.DatasmithScene.GetMetaData(element);

								if (MetaData == null)
								{
									MetaData = new FDatasmithFacadeMetaData("SolidWorks Document Metadata");
									MetaData.SetAssociatedElement(element);
									processor.DatasmithScene.AddMetaData(MetaData);
								}

								foreach (var pair in cmd.MetadataPairs)
								{
									pair.WriteToDatasmithMetaData(MetaData);
								}
							}
						}
						break;

						case CommandType.UPDATE_CAMERA:
                            {
                                // todo doesn't really update, keeps adding changed ones
                                var cmd = command as CameraCommand;
                                bool wasExisting;
                                var dsCamera = processor.GetDatasmithCamera(cmd.camera, out wasExisting);
                                if (!wasExisting)
                                    processor.DatasmithScene.AddActor(dsCamera);
                            }
                            break;
                    }
                }
                else
                {
                    Thread.Sleep(33);
                    idleCount++;
                    if (idleCount == 90) // ~= 3 sec
                    {
                        //SwSingleton.FireStopProgressEvent();
                        SwSingleton.FireProgressEvent("");
                    }
                }
            }
        }

        private static void AddTriangle(List<Triangle> triangles, int i0, int i1, int i2, int matID, Dictionary<int, List<Triangle>> dict)
        {
            var t = new Triangle(i0, i1, i2, matID);
            triangles.Add(t);
            if (!dict.ContainsKey(i0))
                dict.Add(i0, new List<Triangle>());
            dict[i0].Add(t);
            if (!dict.ContainsKey(i1))
                dict.Add(i1, new List<Triangle>());
            dict[i1].Add(t);
            if (!dict.ContainsKey(i2))
                dict.Add(i2, new List<Triangle>());
            dict[i2].Add(t);
        }

        // computes a simple default spherical UV in case everything goes wrong
        //
        private static Vec2 BakeUV(Vec3 v, Vec3 n)
        {
            Vec2 res = new Vec2(0.0f, 0.0f);

            float length = (float)Math.Sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            res.x = (float)Math.Atan2(-v.z, v.x) * (float)(1.0 / Math.PI) * 0.5f;
            if (res.x < 0.0f)
                res.x += 1.0f;
            if (length > 0.0f)
                res.y = (float)Math.Asin(v.y / length) * (float)(1.0 / Math.PI) + 0.5f;
            else
                res.y = 0.5f;
            return res;
        }

        public Geometry.Geometry ProcessChunks(List<Geometry.GeometryChunk> chunks)
        {
            Dictionary<int, List<Triangle>> originalUse = new Dictionary<int, List<Triangle>>();

            List<Triangle> AllTriangles = new List<Triangle>();
            List<Geometry.Vertex> points = new List<Geometry.Vertex>();
            int vertexOffset = 0;
            foreach (var c in chunks)
            {
                bool hasUv = c.TexCoords != null;
                for (int i = 0; i < c.Vertices.Length; i++)
                    points.Add(new Geometry.Vertex(c.Vertices[i] * SwSingleton.GeometryScale, c.Normals[i],
                        hasUv ? c.TexCoords[i] : BakeUV(c.Vertices[i], c.Normals[i]),
                        vertexOffset + i));
                foreach (var t in c.Indices)
                {
                    var ot = t.Offset(vertexOffset);
                    AddTriangle(AllTriangles, ot[0], ot[1], ot[2], t.MaterialID, originalUse);
                }
                vertexOffset += c.Vertices.Length;
            }

            List<Geometry.Vertex> unique = new List<Geometry.Vertex>();
            foreach (var v in points)
                unique.Add(v);

            Geometry.Geometry geom = new Geometry.Geometry();

            geom.Vertices = new Vec3[unique.Count];
            geom.TexCoords = new Vec2[unique.Count];
            geom.Normals = new Vec3[AllTriangles.Count * 3];
            for (int i = 0; i < unique.Count; i++)
            {
                geom.Vertices[i] = unique[i].v;
                geom.TexCoords[i] = unique[i].uv;
            }
            for (int i = 0; i < AllTriangles.Count; i++)
            {
                var t = AllTriangles[i];
                int idx = i * 3;

                geom.Normals[idx] = unique[t.Index1].n;
                geom.Normals[idx + 1] = unique[t.Index2].n;
                geom.Normals[idx + 2] = unique[t.Index3].n;
            }
            geom.Indices = AllTriangles.ToArray();

            return geom;
        }

        private string GetProperNameForMesh(string name)
        {
            name = DatasmithExporter.SanitizeName(name);
            string final = name;
            int inc = 1;
            while (MeshNames.Contains(final))
            {
                final = name + "-" + inc.ToString();
                inc++;
            }
            return final;
        }

        public void CreateMesh(string name, Vec3[] vertices, Vec3[] normals, Vec2[] texcoords, Triangle[] triangles, out FDatasmithFacadeMesh RPCMesh, out FDatasmithFacadeMeshElement RPCMeshElement)
        {
            RPCMeshElement = null;
            RPCMesh = null;

            if (vertices == null || normals == null || texcoords == null || triangles == null)
                return;
            if (vertices.Length == 0 || normals.Length == 0 || texcoords.Length == 0 || triangles.Length == 0)
                return;

            name = GetProperNameForMesh(name);

            RPCMesh = new FDatasmithFacadeMesh();
            RPCMesh.SetName(name);

            RPCMeshElement = new FDatasmithFacadeMeshElement(name);

            RPCMesh.SetVerticesCount(vertices.Length);
            RPCMesh.SetFacesCount(triangles.Length);

            // vertices
            for (int i = 0; i < vertices.Length; i++)
                RPCMesh.SetVertex(i, vertices[i].x, vertices[i].y, vertices[i].z);

            // normals
            for (int i = 0; i < normals.Length; i++)
                RPCMesh.SetNormal(i, normals[i].x, normals[i].y, normals[i].z);

            // texture coords
            if (texcoords != null)
            {
                RPCMesh.SetUVChannelsCount(1);
                RPCMesh.SetUVCount(0, texcoords.Length);
                for (int i = 0; i < texcoords.Length; i++)
                    RPCMesh.SetUV(0, i, texcoords[i].x, texcoords[i].y);
            }

            RPCMeshElement.SetMaterial(DefaultMaterialName, 0);

            HashSet<int> MeshAddedMaterials = new HashSet<int>();

            // triangle indices and materials

            for (int i = 0; i < triangles.Length; i++)
            {
                var t = triangles[i];
                int matID = 0;
                if (t.MaterialID >= 1)
                {
                    bool found = false;

					// Check for material remapping first (fixes duplicate materials)
                    if (SwIDToMat.ContainsKey(t.MaterialID))
                    {
                        var mat = SwIDToMat[t.MaterialID];
						matID = mat.ID;
                        if (!Scene.AddedMaterials.Contains(matID))
                        {
                            Scene.AddedMaterials.Add(matID);
                            DatasmithScene.AddMaterial(SwMat2Datasmith[mat]);
                        }
                        if (!MeshAddedMaterials.Contains(matID))
                        {
                            MeshAddedMaterials.Add(matID);
                            RPCMeshElement.SetMaterial(mat.Name, matID);
                        }
                        found = true;
                    }

					if (!found)
                    { 
                        if (Scene.SwMatID2Mat.ContainsKey(t.MaterialID))
                        {
                            var mat = Scene.SwMatID2Mat[t.MaterialID];
                            if (!Scene.AddedMaterials.Contains(t.MaterialID))
                            {
                                Scene.AddedMaterials.Add(t.MaterialID);
                                DatasmithScene.AddMaterial(SwMat2Datasmith[mat]);
                            }
                            if (!MeshAddedMaterials.Contains(t.MaterialID))
                            {
                                MeshAddedMaterials.Add(t.MaterialID);
                                RPCMeshElement.SetMaterial(mat.Name, t.MaterialID);
                            }
                            matID = t.MaterialID;
                        }
                    }
                }
                RPCMesh.SetFace(i, t[0], t[1], t[2], matID);
                RPCMesh.SetFaceUV(i, 0, t[0], t[1], t[2]);
            }

            DatasmithScene.AddMesh(RPCMeshElement);
        }

        public SwMaterial FindMaterial(int id)
        {
            SwMaterial mat = null;
            if (id >= 0X000DEAD)
                SwIDToMat.TryGetValue(id, out mat);
            if (mat == null)
                Scene.SwMatID2Mat.TryGetValue(id, out mat);
            return mat;
        }
        
        private FDatasmithFacadeActor CreateInstance(string name, string label, FDatasmithFacadeMesh mesh, FDatasmithFacadeMeshElement meshElement, FDatasmithFacadeActor parent = null)
        {
            FDatasmithFacadeActor FacadeActor = null;
            if (mesh != null && mesh.GetVerticesCount() > 0 && mesh.GetFacesCount() > 0)
            {
                FDatasmithFacadeActorMesh RPCMeshActor = new FDatasmithFacadeActorMesh(name);
                string meshName = mesh.GetName();
                RPCMeshActor.SetMesh(meshName);
                FacadeActor = RPCMeshActor;
            }
            else
            {
                //Create a dummy node
                FacadeActor = new FDatasmithFacadeActor(name);
            }

			// ImportBinding uses Tag[0] ('original name') to group parts used in variants
			FacadeActor.AddTag(name);
			FacadeActor.SetLabel(label);

            if (parent == null)
                DatasmithScene.AddActor(FacadeActor);
            else
                parent.AddChild(FacadeActor);

            return FacadeActor;
        }

        public void AddCommand(Command cmd)
        {
            //Queue.RemoveAll(cmd);
            Queue.Push(cmd);
        }

        private FDatasmithFacadeUEPbrMaterial MaterialToDatasmith(SwMaterial material)
        {
            var type = SwMaterial.GetMaterialType(material.ShaderName);

            float roughness = (float)material.Roughness;
            float metallic = 0f;

            if (type != SwMaterial.MaterialType.TYPE_LIGHTWEIGHT)
            {
                if (type == SwMaterial.MaterialType.TYPE_METAL)
                {
                    metallic = 1f;
                    //roughness = (float)MetallicRoughness;
                }
                else if (type == SwMaterial.MaterialType.TYPE_METALLICPAINT)
                    metallic = 0.7f;

                if (material.BlurryReflections)
                    roughness = (float)material.SpecularSpread;
                else
                {
                    if (material.Reflectivity > 0.0)
                        roughness = (1f - (float)material.Reflectivity) * 0.2f;
                    else
                        roughness = 1f;
                }
            }

            var pbr = new FDatasmithFacadeUEPbrMaterial(material.Name);

            double mult = (type == SwMaterial.MaterialType.TYPE_LIGHTWEIGHT) ? material.Diffuse : 1.0;

            double fR = mult * material.PrimaryColor.R * 1.0 / 255.0;
            double fG = mult * material.PrimaryColor.G * 1.0 / 255.0;
            double fB = mult * material.PrimaryColor.B * 1.0 / 255.0;

            var colorExpr = pbr.AddMaterialExpressionColor();
            colorExpr.SetColor((float)fR, (float)fG, (float)fB, 1f);
            colorExpr.SetName("Diffuse Color");
            colorExpr.ConnectExpression(pbr.GetBaseColor());

            FDatasmithFacadeMaterialExpressionScalar roughExpr = pbr.AddMaterialExpressionScalar();
            roughExpr.SetName("Roughness");
            roughExpr.SetScalar(roughness);
            roughExpr.ConnectExpression(pbr.GetRoughness());

            FDatasmithFacadeMaterialExpressionScalar metalExpr = pbr.AddMaterialExpressionScalar();
            metalExpr.SetName("Metallic");
            metalExpr.SetScalar(metallic);
            metalExpr.ConnectExpression(pbr.GetMetallic());

            if (material.Emission > 0.0)
            {
                var emissionExpr = pbr.AddMaterialExpressionColor();
                emissionExpr.SetColor((float)fR, (float)fG, (float)fB, 1f);
                emissionExpr.SetName("Emissive Color");
                emissionExpr.ConnectExpression(pbr.GetEmissiveColor());
            }

            if (material.Transparency > 0.0)
            {
                var opacityExpr = pbr.AddMaterialExpressionScalar();
                opacityExpr.SetName("Opacity");
                opacityExpr.SetScalar(1f - (float)material.Transparency);
                opacityExpr.ConnectExpression(pbr.GetOpacity());

                //pbr.SetBlendMode()
            }

            if (!string.IsNullOrEmpty(material.Texture) && !File.Exists(material.Texture))
            {
                material.Texture = SwMaterial.ComputeAssemblySideTexturePath(material.Texture);
            }

            if (!string.IsNullOrEmpty(material.Texture) && File.Exists(material.Texture))
            {
                string textureName = Path.GetFileNameWithoutExtension(material.Texture);

                FDatasmithFacadeTexture TextureElement = null;
                if (!DatasmithTextures.TryGetValue(material.Texture, out TextureElement))
                {
                    TextureElement = new FDatasmithFacadeTexture(textureName);
                    TextureElement.SetFile(material.Texture);
                    TextureElement.SetTextureFilter(FDatasmithFacadeTexture.ETextureFilter.Default);
                    TextureElement.SetRGBCurve(1);
                    TextureElement.SetTextureAddressX(FDatasmithFacadeTexture.ETextureAddress.Wrap);
                    TextureElement.SetTextureAddressY(FDatasmithFacadeTexture.ETextureAddress.Wrap);
                    FDatasmithFacadeTexture.ETextureMode TextureMode = FDatasmithFacadeTexture.ETextureMode.Diffuse;
                    TextureElement.SetTextureMode(TextureMode);
                    DatasmithScene.AddTexture(TextureElement);
                    DatasmithTextures.Add(material.Texture, TextureElement);
                }

                FDatasmithFacadeMaterialsUtils.FWeightedMaterialExpressionParameters WeightedExpressionParameters = new FDatasmithFacadeMaterialsUtils.FWeightedMaterialExpressionParameters(1f);
                FDatasmithFacadeMaterialsUtils.FUVEditParameters UVParameters = new FDatasmithFacadeMaterialsUtils.FUVEditParameters();
                textureName = DatasmithExporter.SanitizeName(textureName);
                FDatasmithFacadeMaterialExpression TextureExpression = FDatasmithFacadeMaterialsUtils.CreateTextureExpression(pbr, "Diffuse Map", textureName, UVParameters);
                WeightedExpressionParameters.SetColorsRGB(material.PrimaryColor.R, material.PrimaryColor.G, material.PrimaryColor.B, 255);
                WeightedExpressionParameters.SetExpression(TextureExpression);
                FDatasmithFacadeMaterialExpression Expression = FDatasmithFacadeMaterialsUtils.CreateWeightedMaterialExpression(pbr, "Diffuse Color", WeightedExpressionParameters);
                pbr.GetBaseColor().SetExpression(Expression);
            }

            if (!string.IsNullOrEmpty(material.BumpTextureFileName) && !File.Exists(material.BumpTextureFileName))
            {
                material.BumpTextureFileName = SwMaterial.ComputeAssemblySideTexturePath(material.BumpTextureFileName);
            }

            if (!string.IsNullOrEmpty(material.BumpTextureFileName) && File.Exists(material.BumpTextureFileName))
            {
                string textureName = Path.GetFileNameWithoutExtension(material.BumpTextureFileName);

                FDatasmithFacadeTexture TextureElement = null;
                if (!DatasmithTextures.TryGetValue(material.BumpTextureFileName, out TextureElement))
                {
                    TextureElement = new FDatasmithFacadeTexture(textureName);
                    TextureElement.SetFile(material.BumpTextureFileName);
                    TextureElement.SetTextureFilter(FDatasmithFacadeTexture.ETextureFilter.Default);
                    TextureElement.SetRGBCurve(1);
                    TextureElement.SetTextureAddressX(FDatasmithFacadeTexture.ETextureAddress.Wrap);
                    TextureElement.SetTextureAddressY(FDatasmithFacadeTexture.ETextureAddress.Wrap);
                    FDatasmithFacadeTexture.ETextureMode TextureMode = FDatasmithFacadeTexture.ETextureMode.Normal;
                    TextureElement.SetTextureMode(TextureMode);
                    DatasmithScene.AddTexture(TextureElement);
                    DatasmithTextures.Add(material.BumpTextureFileName, TextureElement);
                }

                FDatasmithFacadeMaterialsUtils.FUVEditParameters UVParameters = new FDatasmithFacadeMaterialsUtils.FUVEditParameters();
                FDatasmithFacadeMaterialExpression TextureExpression = FDatasmithFacadeMaterialsUtils.CreateTextureExpression(pbr, "Bump Map", textureName, UVParameters);
                pbr.GetNormal().SetExpression(TextureExpression);
            }

            return pbr;
        }

		// Keep list of all actor bindings for a single variant
		class ComponentMapper
		{
			public ComponentMapper(FDatasmithFacadeVariant InVariant, Processor InProcessor)
			{
				Variant = InVariant;
				Processor = InProcessor;
			}

			public FDatasmithFacadeActorBinding GetActorBinding(string ComponentName)
			{
				FDatasmithFacadeActorBinding Binding = null;
				if (!Bindings.ContainsKey(ComponentName))
				{
					// Find a datasmith actor
					FDatasmithFacadeActor Actor = null;

					if (Processor.Component2DatasmithActor.ContainsKey(ComponentName))
					{
						// The component has been converted either to actor or mesh actor
						Actor = Processor.Component2DatasmithActor[ComponentName].Actor;
						if (Actor == null)
						{
							Actor = Processor.Component2DatasmithActor[ComponentName].MeshActor;
						}
					}
					else
					{
						// Actor was not found, should not happen
						return null;
					}

					// Make a new binding not this
					Binding = new FDatasmithFacadeActorBinding(Actor);
					Bindings.Add(ComponentName, Binding);
					Variant.AddActorBinding(Binding);
				}
				else
				{
					// Get an existing binding
					Binding = Bindings[ComponentName];
				}
				return Binding;
			}

			FDatasmithFacadeVariant Variant;
			Processor Processor;
			Dictionary<string, FDatasmithFacadeActorBinding> Bindings = new Dictionary<string, FDatasmithFacadeActorBinding>();
		};

		private void ConfigurationToDatasmith(string ConfigurationsSetName, ConfigurationDataCommand.Configuration cfg)
		{
#if DATASMITH_VARIANTS
			// Request existing VariantSet, or create a new one
			FDatasmithFacadeLevelVariantSets LevelVariantSets = null;
			FDatasmithFacadeVariantSet VariantSet = null;

			if (DatasmithScene.GetLevelVariantSetsCount() == 0)
			{
				LevelVariantSets = new FDatasmithFacadeLevelVariantSets("LevelVariantSets");
				DatasmithScene.AddLevelVariantSets(LevelVariantSets);
			}
			else
			{
				LevelVariantSets = DatasmithScene.GetLevelVariantSets(0);
            }

			int VariantSetsCount = LevelVariantSets.GetVariantSetsCount();
			for (int VariantSetIndex = 0; VariantSetIndex < VariantSetsCount; ++VariantSetIndex)
			{
				FDatasmithFacadeVariantSet VSet = LevelVariantSets.GetVariantSet(VariantSetIndex);

				if (VSet.GetName() == ConfigurationsSetName){
					VariantSet = VSet;
					break;
				}
			}

			if (VariantSet == null)
			{
				VariantSet = new FDatasmithFacadeVariantSet(ConfigurationsSetName);
				LevelVariantSets.AddVariantSet(VariantSet);
			}

			// Add a new variant
			FDatasmithFacadeVariant Variant = new FDatasmithFacadeVariant(cfg.Name);
			VariantSet.AddVariant(Variant);

			ComponentMapper Mapper = new ComponentMapper(Variant, this);

			// Build a visibility variant data
			foreach (var VisibilityMap in cfg.ComponentVisibility)
			{
				FDatasmithFacadeActorBinding Binding = Mapper.GetActorBinding(VisibilityMap.Key);
				if (Binding != null)
				{
					Binding.AddVisibilityCapture(VisibilityMap.Value);
				}
			}

			// Provide transform variants
			foreach (var TransformMap in cfg.ComponentTransform)
			{
				FDatasmithFacadeActorBinding Binding = Mapper.GetActorBinding(TransformMap.Key);
				if (Binding != null)
				{
					Binding.AddRelativeTransformCapture(TransformMap.Value);
				}
			}

			// Let's remember added materials by their names. Datasmith in UE4 will resolve objects by name anyway.
			HashSet<string> AddedMaterials = new HashSet<string>();

			// Iterate over all material assignments
			foreach (var MaterialMap in cfg.ComponentMaterial)
			{
				FDatasmithFacadeActorBinding Binding = Mapper.GetActorBinding(MaterialMap.Key);
				if (Binding != null)
				{
					SwMaterial mat = MaterialMap.Value;
                    // Set the binding's material
                    GetDatasmithMaterial(mat, true);
                    Binding.AddMaterialCapture(SwMat2Datasmith[mat]);

					// Add the material to Datasmith scene if needed
					if (!AddedMaterials.Contains(mat.Name))
					{
						//scene.AddedMaterials.Add(mat.ID);
						DatasmithScene.AddMaterial(SwMat2Datasmith[mat]);
						AddedMaterials.Add(mat.Name);
					}
				}
			}
#endif // DATASMITH_VARIANTS
		}
	}
}
