// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Runtime.InteropServices;

namespace SolidworksDatasmith.Engine
{
    [ComVisible(false)]
    public enum CommandType
    {
        UPDATE_PART,
        UPDATE_MATERIAL,
        UPDATE_COMPONENT,
        COMPONENT_MATERIAL,
        EXPORT_TO_FILE,
        UPDATE_COMPONENT_TRANSFORM_MULTI,
        ADD_METADATA,
        LIGHTWEIGHT_COMPONENT,
        LIVECONNECT,
		CONFIGURATION_DATA,
        LIVEUPDATE,
        DELETE_COMPONENT,
        UPDATE_CAMERA
    }

    [ComVisible(false)]
    public class Command
    {
        public CommandType Type;
    }

    [ComVisible(false)]
    public class PartCommand : Command
    {
        public string Name { get; set; } = "";
        public string PathName { get; set; } = "";
        public Geometry.StripGeometry StripGeom { get; set; } = null;
		
        public PartCommand()
        {
            Type = CommandType.UPDATE_PART;
        }
    }

    [ComVisible(false)]
    public class MaterialCommand : Command
    {
        public SolidworksDatasmith.SwObjects.SwMaterial Material { get; set; } = null;
        public MaterialCommand()
        {
            Type = CommandType.UPDATE_MATERIAL;
        }
    }

    [ComVisible(false)]
    public class ComponentMaterialCommand : Command
    {
        public SolidworksDatasmith.SwObjects.SwMaterial Material { get; set; } = null;
        public string ComponentName { get; set; }
        public ComponentMaterialCommand()
        {
            Type = CommandType.COMPONENT_MATERIAL;
        }
    }

    [ComVisible(false)]
    public class ComponentCommand : Command
    {
		public string Label { get; set; } = "";
        public string Name { get; set; } = "";
        public string PartName { get; set; } = "";
        public string PartPath { get; set; } = "";
        public string ParentName { get; set; } = "";
        public float[] Transform { get; set; } = null;
        public bool Visible { get; set; } = true;

        public ComponentCommand()
        {
            Type = CommandType.UPDATE_COMPONENT;
        }
    }

    [ComVisible(false)]
    public class DeleteComponentCommand : Command
    {
        public string Name { get; set; } = "";
        public DeleteComponentCommand()
        {
            Type = CommandType.DELETE_COMPONENT;
        }
    }

    [ComVisible(false)]
    public class LightweightComponentCommand : Command
    {
		public string Label { get; set; } = "";
        public string Name { get; set; } = "";
        public string ParentName { get; set; } = "";
        public float[] Transform { get; set; } = null;
        public bool Visible { get; set; } = true;
        public float[] Vertices = null;
        public float[] Normals = null;
        public SwObjects.SwLightweightMaterial Material = null;
        public LightweightComponentCommand()
        {
            Type = CommandType.LIGHTWEIGHT_COMPONENT;
        }
    }

    [ComVisible(false)]
    public class ComponentTransformMultiCommand : Command
    {
        public List<Tuple<string, float[]>> Transforms = new List<Tuple<string, float[]>>();

        public ComponentTransformMultiCommand()
        {
            Type = CommandType.UPDATE_COMPONENT_TRANSFORM_MULTI;
        }
    }

	[ComVisible(false)]
	public class ConfigurationDataCommand : Command
	{
		public class Configuration
		{
			public string Name;
			public Dictionary<string, float[]> ComponentTransform = new Dictionary<string, float[]>();
			public Dictionary<string, bool> ComponentVisibility = new Dictionary<string, bool>();
			public Dictionary<string, SolidworksDatasmith.SwObjects.SwMaterial> ComponentMaterial = new Dictionary<string, SolidworksDatasmith.SwObjects.SwMaterial>();
		};

		public string ConfigurationsSetName = "";
		public List<Configuration> Configurations = new List<Configuration>();

		public ConfigurationDataCommand()
		{
			Type = CommandType.CONFIGURATION_DATA;
		}
	};

	[ComVisible(false)]
    public class ExportCommand : Command
    {
        public string SceneName { get; set; } = "";
        public string Path { get; set; } = "";
        public ExportCommand()
        {
            Type = CommandType.EXPORT_TO_FILE;
        }
    }

    [ComVisible(false)]
    public class MetadataCommand : Command
    {
        public enum MetadataType
        {
            Actor,
            MeshActor,
            None
        }

        public string MetadataOwnerName { get; set; } = "";
        public MetadataType MDataType = MetadataCommand.MetadataType.None;
        public List<SwObjects.SwMetaDataManager.MetadataPair> MetadataPairs = new List<SwObjects.SwMetaDataManager.MetadataPair>();
        public MetadataCommand(MetadataType mdatatype)
        {
            Type = CommandType.ADD_METADATA;
            MDataType = mdatatype;
        }

        public void AddPair(string name, string value)
        {
            MetadataPairs.Add(new SwObjects.SwMetaDataManager.MetadataStringPair(name, value));
        }

        public void AddPair(string name, bool value)
        {
            MetadataPairs.Add(new SwObjects.SwMetaDataManager.MetadataBoolPair(name, value));
        }
    }

    [ComVisible(false)]
    public class LiveUpdateCommand : Command
    {
        public LiveUpdateCommand()
        {
            Type = CommandType.LIVEUPDATE;
        }
    }

    [ComVisible(false)]
    public class CameraCommand : Command
    {
        public SwObjects.SwCamera camera { get; set; } = null;
        public CameraCommand()
        {
            Type = CommandType.UPDATE_CAMERA;
        }
    }

    [ComVisible(false)]
    public class CommandQueue
    {
        private object _lock = new object();
        private List<Command> _commands = new List<Command>();

        public void Push(Command cmd)
        {
            lock (_lock)
            {
                _commands.RemoveAll(x =>
                {
                    if ((x.Type == CommandType.UPDATE_COMPONENT) && (cmd.Type == CommandType.UPDATE_COMPONENT))
                    {
                        var c1 = x as ComponentCommand;
                        var c2 = cmd as ComponentCommand;
                        if (c1.Name == c2.Name)
                            return true;
                    }
                    return false;
                });
                _commands.Add(cmd);
            }
        }

        public Command Pop()
        {
            Command res = null;
            lock (_lock)
            {
                if (_commands.Count > 0)
                {
                    res = _commands[0];
                    _commands.RemoveAt(0);
                }
            }
            return res;
        }

        public void RemoveAll(Command cmd)
        {
            List<Command> support = new List<Command>();
            lock (_lock)
            {
                foreach (var c in _commands)
                {
                    bool isSame = false;
                    if (cmd.Type == c.Type)
                    {
                        switch (cmd.Type)
                        {
                            case CommandType.UPDATE_PART:
                                {
                                    var c1 = cmd as PartCommand;
                                    var c2 = cmd as PartCommand;
                                    if (c1.Name == c2.Name)
                                        isSame = true;
                                }
                                break;

                            case CommandType.UPDATE_COMPONENT:
                                {
                                    var c1 = cmd as ComponentCommand;
                                    var c2 = cmd as ComponentCommand;
                                    if (c1.Name == c2.Name)
                                        isSame = true;
                                }
                                break;

                            case CommandType.UPDATE_MATERIAL:
                                {
                                    var c1 = cmd as MaterialCommand;
                                    var c2 = cmd as MaterialCommand;
                                    if (c1.Material.Name == c2.Material.Name)
                                        isSame = true;
                                }
                                break;

                            case CommandType.EXPORT_TO_FILE:
                                {
                                    var c1 = cmd as ExportCommand;
                                    var c2 = cmd as ExportCommand;
                                    if ((c1.Path == c2.Path) && (c1.SceneName == c2.SceneName))
                                        isSame = true;
                                }
                                break;
                        }
                    }
                    if (!isSame)
                        support.Add(c);
                }
                _commands = support;
            }
        }

        public int Count
        {
            get
            {
                int count = 0;
                lock (_lock)
                {
                    count = _commands.Count;
                }
                return count;
            }
        }
    }
}
