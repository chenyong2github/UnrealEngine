// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Models
{
	/// <summary>
	/// Information about a device attached to an agent
	/// </summary>
	public class DeviceCapabilities
	{
		/// <summary>
		/// Identifier for this device
		/// </summary>
		[BsonRequired]
		public string Handle { get; set; }

		/// <summary>
		/// Properties of this device
		/// </summary>
		[BsonIgnoreIfNull]
		public HashSet<string>? Properties { get; set; }

		/// <summary>
		/// Resources of this device
		/// </summary>
		[BsonIgnoreIfNull]
		public Dictionary<string, int>? Resources { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		[BsonConstructor]
		public DeviceCapabilities()
		{
			Handle = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Handle">Identifier of this device</param>
		/// <param name="Properties">Properties of this device</param>
		/// <param name="Resources">Resources for this device.</param>
		public DeviceCapabilities(string Handle, HashSet<string>? Properties, Dictionary<string, int>? Resources)
		{
			this.Handle = Handle;
			this.Properties = Properties;
			this.Resources = Resources;
		}

		/// <summary>
		/// Construct a device 
		/// </summary>
		/// <param name="Capabilities"></param>
		public DeviceCapabilities(HordeCommon.Rpc.Messages.DeviceCapabilities Capabilities)
			: this(Capabilities.Handle, new HashSet<string>(Capabilities.Properties), new Dictionary<string, int>(Capabilities.Resources))
		{
		}

		/// <summary>
		/// Tests whether this device has the given property
		/// </summary>
		/// <param name="Property"></param>
		/// <returns></returns>
		public bool HasProperty(string Property)
		{
			return Properties != null && Properties.Contains(Property);
		}
	}

	/// <summary>
	/// Capabilities of an agent
	/// </summary>
	public class AgentCapabilities
	{
		/// <summary>
		/// Devices available on this agent
		/// </summary>
		public List<DeviceCapabilities> Devices { get; set; }

		/// <summary>
		/// Properties for this device
		/// </summary>
		[BsonIgnoreIfNull]
		public HashSet<string>? Properties { get; set; }

		/// <summary>
		/// The primary device (the host machine)
		/// </summary>
		public DeviceCapabilities PrimaryDevice => Devices[0];

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		public AgentCapabilities()
		{
			this.Devices = new List<DeviceCapabilities>();
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Devices">Devices attached to this agent</param>
		/// <param name="Properties">Set of properties for this agent</param>
		public AgentCapabilities(List<DeviceCapabilities> Devices, HashSet<string>? Properties)
		{
			this.Devices = Devices;
			this.Properties = Properties;
		}

		/// <summary>
		/// Construct 
		/// </summary>
		/// <param name="Capabilities">RPC capabilities message</param>
		public AgentCapabilities(HordeCommon.Rpc.Messages.AgentCapabilities Capabilities)
			: this(Capabilities.Devices.Select(x => new DeviceCapabilities(x)).ToList(), new HashSet<string>(Capabilities.Properties))
		{
		}
	}

	/// <summary>
	/// Request to create a dervice
	/// </summary>
	public class DeviceRequirements
	{
		/// <summary>
		/// Name of this device
		/// </summary>
		public string? Handle { get; set; }

		/// <summary>
		/// List of properties for this device
		/// </summary>
		public HashSet<string>? Properties { get; set; }

		/// <summary>
		/// Count of resources for this device. This is not included in the RWAPI.
		/// </summary>
		public Dictionary<string, int>? Resources { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private DeviceRequirements()
		{
			Handle = null!;
		}

		/// <summary>
		/// Copy constructor
		/// </summary>
		/// <param name="Other">The other device requirements</param>
		public DeviceRequirements(DeviceRequirements Other)
		{
			this.Handle = Other.Handle;
			if (Other.Properties != null)
			{
				Properties = new HashSet<string>(Other.Properties);
			}
			if (Other.Resources != null)
			{
				Resources = new Dictionary<string, int>(Other.Resources, Other.Resources.Comparer);
			}
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Handle">Name of this device</param>
		/// <param name="Properties">Properties for this device</param>
		/// <param name="Resources">Resources for this device</param>
		public DeviceRequirements(string? Handle, HashSet<string>? Properties, Dictionary<string, int>? Resources)
		{
			this.Handle = Handle;
			this.Properties = Properties;
			this.Resources = Resources;
		}

		/// <summary>
		/// Creates a NodeDevice object from a public API request
		/// </summary>
		/// <param name="Request">The input request</param>
		/// <returns>New NodeDevice instance</returns>
		public static DeviceRequirements FromRequest(CreateDeviceRequirementsRequest Request)
		{
			return new DeviceRequirements(Request.Name, Request.Properties, Request.Resources);
		}

		/// <summary>
		/// Creates an RPC response for this instance
		/// </summary>
		/// <returns>Response object</returns>
		public HordeCommon.Rpc.Messages.DeviceRequirements ToRpcResponse()
		{
			HordeCommon.Rpc.Messages.DeviceRequirements Requirements = new HordeCommon.Rpc.Messages.DeviceRequirements();
			if (Properties != null)
			{
				Requirements.Properties.Add(Properties);
			}
			if (Resources != null)
			{
				Requirements.Resources.Add(Resources);
			}
			return Requirements;
		}
	}

	/// <summary>
	/// Requirements for an agent. Currently mirrors the Worker type in the RWAPI.
	/// </summary>
	public class AgentRequirements
	{
		/// <summary>
		/// Devices attached to this agent
		/// </summary>
		public List<DeviceRequirements>? Devices { get; set; }

		/// <summary>
		/// Properties of this agent
		/// </summary>
		public HashSet<string>? Properties { get; set; }

		/// <summary>
		/// Configurable settings for this agent
		/// </summary>
		public Dictionary<string, string>? Configs { get; set; }

		/// <summary>
		/// Whether the agent can be shared with another step
		/// </summary>
		public bool Shared { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public AgentRequirements()
		{
			this.Devices = new List<DeviceRequirements>();
		}

		/// <summary>
		/// Copy constructor
		/// </summary>
		/// <param name="Other">Instance to copy from</param>
		public AgentRequirements(AgentRequirements Other)
		{
			if (Other.Devices != null)
			{
				this.Devices = Other.Devices.ConvertAll(x => new DeviceRequirements(x));
			}
			if(Other.Properties != null)
			{
				this.Properties = new HashSet<string>(Other.Properties);
			}
			if(Other.Configs != null)
			{
				this.Configs = new Dictionary<string, string>(Other.Configs);
			}
			this.Shared = Other.Shared;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Devices">Devices attached to this agent</param>
		/// <param name="Properties">Properties of this agent</param>
		/// <param name="Configs">Configuration settings for this agent</param>
		/// <param name="Shared">Whether the agent can be shared with another job</param>
		public AgentRequirements(List<DeviceRequirements>? Devices, HashSet<string>? Properties, Dictionary<string, string>? Configs, bool Shared)
		{
			this.Devices = Devices;
			this.Properties = Properties;
			this.Configs = Configs;
			this.Shared = Shared;
		}

		/// <summary>
		/// Determine if this requirements object is empty
		/// </summary>
		/// <returns>TRue if the object is empty</returns>
		public bool IsEmpty()
		{
			return Devices == null && Properties == null && Configs == null && !Shared;
		}

		/// <summary>
		/// Constructs a NodeAgent object from a public API request
		/// </summary>
		/// <param name="Request">The API request</param>
		/// <returns>The new node agent instance</returns>
		[return: NotNullIfNotNull("Request")]
		public static AgentRequirements? FromRequest(CreateAgentRequirementsRequest? Request)
		{
			if (Request == null)
			{
				return null;
			}

			List<DeviceRequirements>? Devices = null;
			if (Request.Devices != null)
			{
				Devices = Request.Devices?.ConvertAll(x => DeviceRequirements.FromRequest(x));
			}

			HashSet<string>? Properties = null;
			if (Request.Properties != null)
			{
				Properties = new HashSet<string>(Request.Properties, StringComparer.Ordinal);
			}

			Dictionary<string, string>? Configs = null;
			if (Request.Configs != null)
			{
				Configs = new Dictionary<string, string>(Request.Configs, StringComparer.Ordinal);
			}

			return new AgentRequirements(Devices, Properties, Configs, Request.Shared);
		}

		/// <summary>
		/// Creates an Rpc response object for this instance
		/// </summary>
		/// <returns>Response object</returns>
		public HordeCommon.Rpc.Messages.AgentRequirements ToRpcResponse()
		{
			HordeCommon.Rpc.Messages.AgentRequirements Requirements = new HordeCommon.Rpc.Messages.AgentRequirements();
			if (Devices != null)
			{
				Requirements.Devices.AddRange(Devices.Select(x => x.ToRpcResponse()));
			}
			if (Properties != null)
			{
				Requirements.Properties.AddRange(Properties);
			}
			Requirements.Shared = Shared;
			return Requirements;
		}
	}
}
