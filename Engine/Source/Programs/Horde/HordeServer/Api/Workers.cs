// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Api
{
	/// <summary>
	/// Information about a device attached to this agent
	/// </summary>
	public class GetDeviceCapabilitiesResponse
	{
		/// <summary>
		/// Logical name of this device
		/// </summary>
		[Required]
		public string Name { get; set; }

		/// <summary>
		/// Required properties for this device, in the form "KEY=VALUE"
		/// </summary>
		public List<string>? Properties { get; set; }

		/// <summary>
		/// Required resources for this node. If null, the node will assume exclusive access to the device.
		/// </summary>
		public Dictionary<string, int>? Resources { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private GetDeviceCapabilitiesResponse()
		{
			this.Name = null!;
		}

		/// <summary>
		/// Converts this object to an API response
		/// </summary>
		/// <returns>Response object</returns>
		public GetDeviceCapabilitiesResponse(DeviceCapabilities DeviceCapabilities)
		{
			this.Name = DeviceCapabilities.Handle;
			this.Properties = DeviceCapabilities.Properties.ToList();
			this.Resources = DeviceCapabilities.Resources;
		}
	}

	/// <summary>
	/// Information about the capabilities of this agent
	/// </summary>
	public class GetAgentCapabilitiesResponse
	{
		/// <summary>
		/// Information about the devices required for this node to run
		/// </summary>
		public List<GetDeviceCapabilitiesResponse>? Devices { get; set; }

		/// <summary>
		/// Global agent properties for this node
		/// </summary>
		public List<string>? Properties { get; set; }

		/// <summary>
		/// Default constructor
		/// </summary>
		public GetAgentCapabilitiesResponse()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AgentCapabilities">The agent capabiilities to construct from</param>
		public GetAgentCapabilitiesResponse(AgentCapabilities AgentCapabilities)
		{
			this.Devices = AgentCapabilities.Devices.ConvertAll(x => new GetDeviceCapabilitiesResponse(x));
			this.Properties = AgentCapabilities.Properties?.ToList();
		}
	}

	/// <summary>
	/// Information about the device requirements of a node
	/// </summary>
	public class CreateDeviceRequirementsRequest
	{
		/// <summary>
		/// Logical name of this device
		/// </summary>
		public string? Name { get; set; }

		/// <summary>
		/// Required properties for this device, in the form "KEY=VALUE"
		/// </summary>
		public HashSet<string>? Properties { get; set; }

		/// <summary>
		/// Required resources for this node. If null, the node will assume exclusive access to the device.
		/// </summary>
		public Dictionary<string, int>? Resources { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private CreateDeviceRequirementsRequest()
		{
			this.Name = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name">Logical name of this device</param>
		public CreateDeviceRequirementsRequest(string Name)
		{
			this.Name = Name;
		}
	}

	/// <summary>
	/// Information about the agent requirements of node
	/// </summary>
	public class CreateAgentRequirementsRequest
	{
		/// <summary>
		/// Information about the devices required for this node to run
		/// </summary>
		public List<CreateDeviceRequirementsRequest>? Devices { get; set; }

		/// <summary>
		/// Global agent properties for this node
		/// </summary>
		public List<string>? Properties { get; set; }

		/// <summary>
		/// Configurable settings for this agent
		/// </summary>
		public Dictionary<string, string>? Configs { get; set; }

		/// <summary>
		/// Whether the agent can be shared with another job
		/// </summary>
		public bool Shared { get; set; }
	}

	/// <summary>
	/// Information about the device requirements of a node
	/// </summary>
	public class GetDeviceRequirementsResponse
	{
		/// <summary>
		/// Logical name of this device
		/// </summary>
		[Required]
		public string? Name { get; set; }

		/// <summary>
		/// Required properties for this device, in the form "KEY=VALUE"
		/// </summary>
		public HashSet<string>? Properties { get; set; }

		/// <summary>
		/// Required resources for this node. If null, the node will assume exclusive access to the device.
		/// </summary>
		public Dictionary<string, int>? Resources { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private GetDeviceRequirementsResponse()
		{
			this.Name = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DeviceRequirements">The device requirements</param>
		public GetDeviceRequirementsResponse(DeviceRequirements DeviceRequirements)
		{
			this.Name = DeviceRequirements.Handle;
			this.Properties = DeviceRequirements.Properties;
			this.Resources = DeviceRequirements.Resources;
		}
	}

	/// <summary>
	/// Information about the agent requirements of node
	/// </summary>
	public class GetAgentRequirementsResponse
	{
		/// <summary>
		/// Information about the devices required for this node to run
		/// </summary>
		public List<GetDeviceRequirementsResponse>? Devices { get; set; }

		/// <summary>
		/// Global agent properties for this node
		/// </summary>
		public List<string>? Properties { get; set; }

		/// <summary>
		/// Whether the agent can be shared with another job
		/// </summary>
		public bool Shared { get; set; }

		/// <summary>
		/// Default constructor
		/// </summary>
		public GetAgentRequirementsResponse()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AgentRequirements">Agent requirements to construct from</param>
		public GetAgentRequirementsResponse(AgentRequirements AgentRequirements)
		{
			this.Devices = AgentRequirements.Devices?.ConvertAll(x => new GetDeviceRequirementsResponse(x));
			this.Properties = AgentRequirements.Properties?.ToList();
			this.Shared = AgentRequirements.Shared;
		}
	}
}
