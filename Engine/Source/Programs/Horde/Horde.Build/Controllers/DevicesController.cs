// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;

namespace HordeServer.Controllers
{
	using DeviceId = StringId<IDevice>;
	using DevicePlatformId = StringId<IDevicePlatform>;
	using DevicePoolId = StringId<IDevicePool>;
	using ProjectId = StringId<IProject>;

	/// <summary>
	/// Controller for device service
	/// </summary>
	public class DevicesController : ControllerBase
	{

		/// <summary>
		/// The acl service singleton
		/// </summary>
		AclService AclService;

		/// <summary>
		/// The user collection instance
		/// </summary>
		IUserCollection UserCollection { get; set; }


		/// <summary>
		/// Singleton instance of the device service
		/// </summary>
		DeviceService DeviceService;

		/// <summary>
		///  Logger for controller
		/// </summary>
		private readonly ILogger<DevicesController> Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public DevicesController(DeviceService DeviceService, AclService AclService, IUserCollection UserCollection, ILogger<DevicesController> Logger)
		{
			this.UserCollection = UserCollection;
			this.DeviceService = DeviceService;
			this.Logger = Logger;
			this.AclService = AclService;
		}

		// DEVICES

		/// <summary>
		/// Create a new device
		/// </summary>
		[HttpPost]
		[Authorize]
		[Route("/api/v2/devices")]
		public async Task<ActionResult<CreateDeviceResponse>> CreateDeviceAsync([FromBody] CreateDeviceRequest DeviceRequest)
		{

			DevicePoolAuthorization? PoolAuth = await DeviceService.GetUserPoolAuthorizationAsync(new DevicePoolId(DeviceRequest.PoolId!), User);

			if (PoolAuth == null || !PoolAuth.Write)
			{
				return Forbid();
			}

			IUser? InternalUser = await UserCollection.GetUserAsync(User);
			if (InternalUser == null)
			{
				return NotFound();
			}

			IDevicePlatform? Platform = await DeviceService.GetPlatformAsync(new DevicePlatformId(DeviceRequest.PlatformId!));

			if (Platform == null)
			{
				return BadRequest($"Bad platform id {DeviceRequest.PlatformId} on request");
			}

			IDevicePool? Pool = await DeviceService.GetPoolAsync(new DevicePoolId(DeviceRequest.PoolId!));

			if (Pool == null)
			{
				return BadRequest($"Bad pool id {DeviceRequest.PoolId} on request");
			}


			string? ModelId = null;

			if (!string.IsNullOrEmpty(DeviceRequest.ModelId))
			{
				ModelId = new string(DeviceRequest.ModelId);

				if (Platform.Models?.FirstOrDefault(x => x == ModelId) == null)
				{
					return BadRequest($"Bad model id {ModelId} for platform {Platform.Id} on request");
				}
			}

			string Name = DeviceRequest.Name.Trim();
			string? Address = DeviceRequest.Address?.Trim();

			IDevice? Device = await DeviceService.TryCreateDeviceAsync(DeviceId.Sanitize(Name), Name, Platform.Id, Pool.Id, DeviceRequest.Enabled, Address, ModelId, InternalUser.Id);

			if (Device == null)
			{
				return BadRequest($"Unable to create device");
			}

			return new CreateDeviceResponse(Device.Id.ToString());
		}

		/// <summary>
		/// Get list of devices
		/// </summary>
		[HttpGet]
		[Authorize]
		[Route("/api/v2/devices")]
		[ProducesResponseType(typeof(List<GetDeviceResponse>), 200)]
		public async Task<ActionResult<List<object>>> GetDevicesAsync()
		{

			List<DevicePoolAuthorization> PoolAuth = await DeviceService.GetUserPoolAuthorizationsAsync(User);

			List<IDevice> Devices = await DeviceService.GetDevicesAsync();

			List<object> Responses = new List<object>();

			foreach (IDevice Device in Devices)
			{
				DevicePoolAuthorization? Auth = PoolAuth.Where(x => x.Pool.Id == Device.PoolId).FirstOrDefault();

				if (Auth == null || !Auth.Read)
				{
					continue;
				}

				Responses.Add(new GetDeviceResponse(Device.Id.ToString(), Device.PlatformId.ToString(), Device.PoolId.ToString(), Device.Name, Device.Enabled, Device.Address, Device.ModelId?.ToString(), Device.ModifiedByUser, Device.Notes, Device.ProblemTimeUtc, Device.MaintenanceTimeUtc, Device.Utilization, Device.CheckedOutByUser, Device.CheckOutTime));
			}

			return Responses;
		}

		/// <summary>
		/// Get a specific device
		/// </summary>
		[HttpGet]
		[Authorize]
		[Route("/api/v2/devices/{DeviceId}")]
		[ProducesResponseType(typeof(GetDeviceResponse), 200)]
		public async Task<ActionResult<object>> GetDeviceAsync(string DeviceId)
		{

			DeviceId DeviceIdValue = new DeviceId(DeviceId);

			IDevice? Device = await DeviceService.GetDeviceAsync(DeviceIdValue);

			if (Device == null)
			{
				return BadRequest($"Unable to find device with id {DeviceId}");
			}

			DevicePoolAuthorization? PoolAuth = await DeviceService.GetUserPoolAuthorizationAsync(Device.PoolId, User);

			if (PoolAuth == null || !PoolAuth.Write)
			{
				return Forbid();
			}


			return new GetDeviceResponse(Device.Id.ToString(), Device.PlatformId.ToString(), Device.PoolId.ToString(), Device.Name, Device.Enabled, Device.Address, Device.ModelId?.ToString(), Device.ModifiedByUser?.ToString(), Device.Notes, Device.ProblemTimeUtc, Device.MaintenanceTimeUtc, Device.Utilization, Device.CheckedOutByUser, Device.CheckOutTime);
		}

		/// <summary>
		/// Update a specific device
		/// </summary>
		[HttpPut]
		[Authorize]
		[Route("/api/v2/devices/{DeviceId}")]
		[ProducesResponseType(typeof(List<GetDeviceResponse>), 200)]
		public async Task<ActionResult> UpdateDeviceAsync(string DeviceId, [FromBody] UpdateDeviceRequest Update)
		{
			IUser? InternalUser = await UserCollection.GetUserAsync(User);
			if (InternalUser == null)
			{
				return NotFound();
			}

			DeviceId DeviceIdValue = new DeviceId(DeviceId);

			IDevice? Device = await DeviceService.GetDeviceAsync(DeviceIdValue);

			if (Device == null)
			{
				return BadRequest($"Device with id ${DeviceId} does not exist");
			}

			DevicePoolAuthorization? PoolAuth = await DeviceService.GetUserPoolAuthorizationAsync(Device.PoolId, User);

			if (PoolAuth == null || !PoolAuth.Write)
			{
				return Forbid();
			}

			IDevicePlatform? Platform = await DeviceService.GetPlatformAsync(Device.PlatformId);

			if (Platform == null)
			{
				return BadRequest($"Platform id ${Device.PlatformId} does not exist");
			}

			DevicePoolId? PoolIdValue = null;

			if (Update.PoolId != null)
			{
				PoolIdValue = new DevicePoolId(Update.PoolId);
			}

			string? ModelIdValue = null;

			if (Update.ModelId != null)
			{
				ModelIdValue = new string(Update.ModelId);

				if (Platform.Models?.FirstOrDefault(x => x == ModelIdValue) == null)
				{
					return BadRequest($"Bad model id {Update.ModelId} for platform {Platform.Id} on request");
				}

			}

			string? Name = Update.Name?.Trim();
			string? Address = Update.Address?.Trim();

            await DeviceService.UpdateDeviceAsync(DeviceIdValue, PoolIdValue, Name, Address, ModelIdValue, Update.Notes, Update.Enabled, Update.Problem, Update.Maintenance, InternalUser.Id);

			return Ok();
		}

		/// <summary>
		/// Checkout a device
		/// </summary>
		[HttpPut]
		[Authorize]
		[Route("/api/v2/devices/{DeviceId}/checkout")]
		[ProducesResponseType(typeof(List<GetDeviceResponse>), 200)]
		public async Task<ActionResult> CheckoutDeviceAsync(string DeviceId, [FromBody] CheckoutDeviceRequest Request)
		{

			IUser? InternalUser = await UserCollection.GetUserAsync(User);
			if (InternalUser == null)
			{
				return NotFound();
			}

			DeviceId DeviceIdValue = new DeviceId(DeviceId);

			IDevice? Device = await DeviceService.GetDeviceAsync(DeviceIdValue);

			if (Device == null)
			{
				return BadRequest($"Device with id ${DeviceId} does not exist");
			}

			DevicePoolAuthorization? PoolAuth = await DeviceService.GetUserPoolAuthorizationAsync(Device.PoolId, User);

			if (PoolAuth == null || !PoolAuth.Write)
			{
				return Forbid();
			}

			if (Request.Checkout)
			{
				if (!string.IsNullOrEmpty(Device.CheckedOutByUser))
				{
					return BadRequest($"Already checked out by user {Device.CheckedOutByUser}");
				}

                await DeviceService.CheckoutDeviceAsync(DeviceIdValue, InternalUser.Id);

            }
			else
			{
				await DeviceService.CheckoutDeviceAsync(DeviceIdValue, null);
			}

			return Ok();
		}


		/// <summary>
		/// Delete a specific device
		/// </summary>
		[HttpDelete]
		[Authorize]
		[Route("/api/v2/devices/{DeviceId}")]
		public async Task<ActionResult> DeleteDeviceAsync(string DeviceId)
		{
			if (!await DeviceService.AuthorizeAsync(AclAction.DeviceWrite, User))
			{
				return Forbid();
			}

			DeviceId DeviceIdValue = new DeviceId(DeviceId);

			IDevice? Device = await DeviceService.GetDeviceAsync(DeviceIdValue);
			if (Device == null)
			{
				return NotFound();
			}

			DevicePoolAuthorization? PoolAuth = await DeviceService.GetUserPoolAuthorizationAsync(Device.PoolId, User);

			if (PoolAuth == null || !PoolAuth.Write)
			{
				return Forbid();
			}


			await DeviceService.DeleteDeviceAsync(DeviceIdValue);
			return Ok();
		}

		// PLATFORMS

		/// <summary>
		/// Create a new device platform
		/// </summary>
		[HttpPost]
		[Authorize]
		[Route("/api/v2/devices/platforms")]
		public async Task<ActionResult<CreateDevicePlatformResponse>> CreatePlatformAsync([FromBody] CreateDevicePlatformRequest Request)
		{
			if (!await DeviceService.AuthorizeAsync(AclAction.DeviceWrite, User))
			{
				return Forbid();
			}

			string Name = Request.Name.Trim();

			IDevicePlatform? Platform = await DeviceService.TryCreatePlatformAsync(DevicePlatformId.Sanitize(Name), Name);

			if (Platform == null)
			{
				return BadRequest($"Unable to create platform for {Name}");
			}

			return new CreateDevicePlatformResponse(Platform.Id.ToString());


		}

		/// <summary>
		/// Update a device platform
		/// </summary>
		[HttpPut]
		[Authorize]
		[Route("/api/v2/devices/platforms/{PlatformId}")]
		public async Task<ActionResult<CreateDevicePlatformResponse>> UpdatePlatformAsync(string PlatformId, [FromBody] UpdateDevicePlatformRequest Request)
		{
			if (!await DeviceService.AuthorizeAsync(AclAction.DeviceWrite, User))
			{
				return Forbid();
			}


			await DeviceService.UpdatePlatformAsync(new DevicePlatformId(PlatformId), Request.ModelIds);

			return Ok();


		}

		/// <summary>
		/// Get a list of supported device platforms
		/// </summary>
		[HttpGet]
		[Authorize]
		[Route("/api/v2/devices/platforms")]
		[ProducesResponseType(typeof(List<GetDevicePlatformResponse>), 200)]
		public async Task<ActionResult<List<object>>> GetDevicePlatformsAsync()
		{

			if (!await DeviceService.AuthorizeAsync(AclAction.DeviceRead, User))
			{
				return Forbid();
			}

			List<IDevicePlatform> Platforms = await DeviceService.GetPlatformsAsync();

			List<object> Responses = new List<object>();

			foreach (IDevicePlatform Platform in Platforms)
			{
				// @todo: ACL per platform
				Responses.Add(new GetDevicePlatformResponse(Platform.Id.ToString(), Platform.Name, Platform.Models?.ToArray() ?? Array.Empty<string>()));
			}

			return Responses;
		}

		// POOLS

		/// <summary>
		/// Create a new device pool
		/// </summary>
		[HttpPost]
		[Authorize]
		[Route("/api/v2/devices/pools")]
		public async Task<ActionResult<CreateDevicePoolResponse>> CreatePoolAsync([FromBody] CreateDevicePoolRequest Request)
		{
			if (!await DeviceService.AuthorizeAsync(AclAction.DeviceWrite, User))
			{
				return Forbid();
			}

			string Name = Request.Name.Trim();

			IDevicePool? Pool = await DeviceService.TryCreatePoolAsync(DevicePoolId.Sanitize(Name), Name, Request.PoolType, Request.ProjectIds?.Select(x => new ProjectId(x)).ToList());

			if (Pool == null)
			{
				return BadRequest($"Unable to create pool for {Request.Name}");
			}

			return new CreateDevicePoolResponse(Pool.Id.ToString());

		}

		/// <summary>
		/// Update a device pool
		/// </summary>
		[HttpPut]
		[Authorize]
		[Route("/api/v2/devices/pools")]
		public async Task<ActionResult> UpdatePoolAsync([FromBody] UpdateDevicePoolRequest Request)
		{

			DevicePoolAuthorization? PoolAuth = await DeviceService.GetUserPoolAuthorizationAsync(new DevicePoolId(Request.Id), User);

			if (PoolAuth == null || !PoolAuth.Write)
			{
				return Forbid();
			}

			List<ProjectId>? ProjectIds = null;

			if (Request.ProjectIds != null)
			{
				ProjectIds = Request.ProjectIds.Select(x => new ProjectId(x)).ToList();
			}

			await DeviceService.UpdatePoolAsync(new DevicePoolId(Request.Id), ProjectIds);
			
			return Ok();
		}


		/// <summary>
		/// Get a list of existing device pools
		/// </summary>
		[HttpGet]
		[Authorize]
		[Route("/api/v2/devices/pools")]
		[ProducesResponseType(typeof(List<GetDevicePoolResponse>), 200)]
		public async Task<ActionResult<List<object>>> GetDevicePoolsAsync()
		{

			List<DevicePoolAuthorization> PoolAuth = await DeviceService.GetUserPoolAuthorizationsAsync(User);

			List<IDevicePool> Pools = await DeviceService.GetPoolsAsync();

			List<object> Responses = new List<object>();

			foreach (IDevicePool Pool in Pools)
			{
				DevicePoolAuthorization? Auth = PoolAuth.Where(x => x.Pool.Id == Pool.Id).FirstOrDefault();

				if (Auth == null || !Auth.Read)
				{
					continue;
				}
				
				Responses.Add(new GetDevicePoolResponse(Pool.Id.ToString(), Pool.Name, Pool.PoolType, Auth.Write));
			}

			return Responses;
		}

		// RESERVATIONS

		/// <summary>
		/// Create a new device reservation
		/// </summary>
		[HttpPost]
		[Authorize]
		[Route("/api/v2/devices/reservations")]
		[ProducesResponseType(typeof(CreateDeviceReservationResponse), 200)]
		public async Task<ActionResult<CreateDeviceReservationResponse>> CreateDeviceReservation([FromBody] CreateDeviceReservationRequest Request)
		{

			if (!await DeviceService.AuthorizeAsync(AclAction.DeviceWrite, User))
			{
				return Forbid();
			}

			List<IDevicePool> Pools = await DeviceService.GetPoolsAsync();
			List<IDevicePlatform> Platforms = await DeviceService.GetPlatformsAsync();

			DevicePoolId PoolIdValue = new DevicePoolId(Request.PoolId);
			IDevicePool? Pool = Pools.FirstOrDefault(x => x.Id == PoolIdValue);
			if (Pool == null)
			{
				return BadRequest($"Unknown pool {Request.PoolId} on device reservation request");
			}

			List<DeviceRequestData> RequestedDevices = new List<DeviceRequestData>();

			foreach (DeviceReservationRequest DeviceRequest in Request.Devices)
			{
				DevicePlatformId PlatformIdValue = new DevicePlatformId(DeviceRequest.PlatformId);
				IDevicePlatform? Platform = Platforms.FirstOrDefault(x => x.Id == PlatformIdValue);

				if (Platform == null)
				{
					return BadRequest($"Unknown platform {DeviceRequest.PlatformId} on device reservation request");
				}

				if (DeviceRequest.IncludeModels != null)
				{
					foreach (string Model in DeviceRequest.IncludeModels)
					{
						if (Platform.Models?.FirstOrDefault(x => x == Model) == null)
						{
							return BadRequest($"Unknown model {Model} for platform {DeviceRequest.PlatformId} on device reservation request");
						}

					}

				}

				if (DeviceRequest.ExcludeModels != null)
				{
					foreach (string Model in DeviceRequest.ExcludeModels)
					{
						if (Platform.Models?.FirstOrDefault(x => x == Model) == null)
						{
							return BadRequest($"Unknown model {Model} for platform {DeviceRequest.PlatformId} on device reservation request");
						}

					}

				}					

				RequestedDevices.Add(new DeviceRequestData(PlatformIdValue, DeviceRequest.IncludeModels, DeviceRequest.ExcludeModels));

			}

			IDeviceReservation? Reservation = await DeviceService.TryCreateReservationAsync(PoolIdValue, RequestedDevices);

			if (Reservation == null)
			{
				return Conflict("Unable to allocated devices for reservation");
			}

			List<IDevice> Devices = await DeviceService.GetDevicesAsync(Reservation.Devices);


			CreateDeviceReservationResponse Response = new CreateDeviceReservationResponse();

			Response.Id = Reservation.Id.ToString();

			foreach (IDevice Device in Devices)
			{
				Response.Devices.Add(new GetDeviceResponse(Device.Id.ToString(), Device.PlatformId.ToString(), Device.PoolId.ToString(), Device.Name, Device.Enabled, Device.Address, Device.ModelId?.ToString(), Device.ModifiedByUser, Device.Notes, Device.ProblemTimeUtc, Device.MaintenanceTimeUtc, Device.Utilization));
			}

			return Response;
		}

		/// <summary>
		/// Get active device reservations
		/// </summary>		
		[HttpGet]
		[Authorize]
		[Route("/api/v2/devices/reservations")]
		[ProducesResponseType(typeof(List<GetDeviceReservationResponse>), 200)]
		public async Task<ActionResult<List<GetDeviceReservationResponse>>> GetDeviceReservations()
		{
			if (!await DeviceService.AuthorizeAsync(AclAction.DeviceRead, User))
			{
				return Forbid();
			}

			List<IDeviceReservation> Reservations = await DeviceService.GetReservationsAsync();

			List<GetDeviceReservationResponse> Response = new List<GetDeviceReservationResponse>();

			foreach (IDeviceReservation Reservation in Reservations)
			{
				GetDeviceReservationResponse ReservationResponse = new GetDeviceReservationResponse()
				{
					Id = Reservation.Id.ToString(),
					PoolId = Reservation.PoolId.ToString(),
					Devices = Reservation.Devices.Select(x => x.ToString()).ToList(),
					JobId = Reservation.JobId?.ToString(),
					StepId = Reservation.StepId?.ToString(),
					UserId = Reservation.UserId?.ToString(),
					Hostname = Reservation.Hostname,
					ReservationDetails = Reservation.ReservationDetails,
					CreateTimeUtc = Reservation.CreateTimeUtc,
					LegacyGuid = Reservation.LegacyGuid
				};

				Response.Add(ReservationResponse);

			}

			return Response;
		}

		/// <summary>
		/// Renew an existing reservation
		/// </summary>
		[HttpPut]
		[Authorize]
		[Route("/api/v2/devices/reservations/{ReservationId}")]
		public async Task<ActionResult> UpdateReservationAsync(string ReservationId)
		{
			if (!await DeviceService.AuthorizeAsync(AclAction.DeviceWrite, User))
			{
				return Forbid();
			}

			ObjectId ReservationIdValue = new ObjectId(ReservationId);

			bool Updated = await DeviceService.TryUpdateReservationAsync(ReservationIdValue);

			if (!Updated)
			{
				return BadRequest("Failed to update reservation");
			}

			return Ok();
		}

		/// <summary>
		/// Delete a reservation
		/// </summary>
		[HttpDelete]
		[Authorize]
		[Route("/api/v2/devices/reservations/{ReservationId}")]
		public async Task<ActionResult> DeleteReservationAsync(string ReservationId)
		{
			if (!await DeviceService.AuthorizeAsync(AclAction.DeviceWrite, User))
			{
				return Forbid();
			}

			ObjectId ReservationIdValue = new ObjectId(ReservationId);

			bool Deleted = await DeviceService.DeleteReservationAsync(ReservationIdValue);

			if (!Deleted)
			{
				return BadRequest("Failed to delete reservation");
			}

			return Ok();
		}

		// LEGACY V1 API

		enum LegacyPerfSpec
		{
			Unspecified,
			Minimum,
			Recommended,
			High
		};

		/// <summary>
		/// Create a device reservation
		/// </summary>
		[HttpPost]
		[Route("/api/v1/reservations")]
		[ProducesResponseType(typeof(GetLegacyReservationResponse), 200)]
		public async Task<ActionResult<GetLegacyReservationResponse>> CreateDeviceReservationV1Async([FromBody] LegacyCreateReservationRequest Request)
		{

			List<IDevicePool> Pools = await DeviceService.GetPoolsAsync();
			List<IDevicePlatform> Platforms = await DeviceService.GetPlatformsAsync();

			string Message;
			string? PoolId = Request.PoolId;

			// @todo: Remove this once all streams are updated to provide jobid
			string Details = "";
			if ((String.IsNullOrEmpty(Request.JobId) || String.IsNullOrEmpty(Request.StepId)))
			{
				if (!string.IsNullOrEmpty(Request.ReservationDetails))
				{
					Details = $" - {Request.ReservationDetails}";
				}
				else
				{
					Details = $" - Host {Request.Hostname}";
				}
			}

			if (string.IsNullOrEmpty(PoolId))
			{
				Message = $"No pool specified, defaulting to UE4" + Details;
				Logger.LogError(Message + $" JobId: {Request.JobId}, StepId: {Request.StepId}");
				await DeviceService.NotifyDeviceServiceAsync(Message, null, Request.JobId, Request.StepId);
				PoolId = "ue4";
				//return BadRequest(Message);
			}

			DevicePoolId PoolIdValue = DevicePoolId.Sanitize(PoolId);
			IDevicePool? Pool = Pools.FirstOrDefault(x => x.Id == PoolIdValue);
			if (Pool == null)
			{
				Message = $"Unknown pool {PoolId} " + Details;
				Logger.LogError(Message);
				await DeviceService.NotifyDeviceServiceAsync(Message, null, Request.JobId, Request.StepId);
				return BadRequest(Message);
			}

			List<DeviceRequestData> RequestedDevices = new List<DeviceRequestData>();

			List<string> PerfSpecs = new List<string>();

			foreach (string DeviceType in Request.DeviceTypes)
			{

				string PlatformName = DeviceType;
				string PerfSpecName = "Unspecified";

				if (DeviceType.Contains(":", StringComparison.OrdinalIgnoreCase))
				{
					string[] Tokens = DeviceType.Split(":");
					PlatformName = Tokens[0];
					PerfSpecName = Tokens[1];
				}

				PerfSpecs.Add(PerfSpecName);

				DevicePlatformId PlatformId;

				DevicePlatformMapV1 MapV1 = await DeviceService.GetPlatformMapV1();

				if (!MapV1.PlatformMap.TryGetValue(PlatformName, out PlatformId))
				{
					Message = $"Unknown platform {PlatformName}" + Details;
					Logger.LogError(Message);
					await DeviceService.NotifyDeviceServiceAsync(Message, null, Request.JobId, Request.StepId);
					return BadRequest(Message);
				}

				IDevicePlatform? Platform = Platforms.FirstOrDefault(x => x.Id == PlatformId);

				if (Platform == null)
				{
					Message = $"Unknown platform {PlatformId}" + Details;
					Logger.LogError(Message);
					await DeviceService.NotifyDeviceServiceAsync(Message, null, Request.JobId, Request.StepId);
					return BadRequest(Message);
				}

				List<string> IncludeModels = new List<string>();
				List<string> ExcludeModels = new List<string>();

				if (PerfSpecName == "High")
				{
					string? Model = null;
					if (MapV1.PerfSpecHighMap.TryGetValue(PlatformId, out Model))
					{
						IncludeModels.Add(Model);
					}
				}

				if (PerfSpecName == "Minimum" || PerfSpecName == "Recommended")
				{
					string? Model = null;
					if (MapV1.PerfSpecHighMap.TryGetValue(PlatformId, out Model))
					{
						ExcludeModels.Add(Model);
					}
				}


				RequestedDevices.Add(new DeviceRequestData(PlatformId, IncludeModels, ExcludeModels));

			}

			IDeviceReservation? Reservation = await DeviceService.TryCreateReservationAsync(PoolIdValue, RequestedDevices, Request.Hostname, Request.ReservationDetails, Request.JobId, Request.StepId);

			if (Reservation == null)
			{
				return Conflict("Unable to allocated devices for reservation");
			}

			List<IDevice> Devices = await DeviceService.GetDevicesAsync(Reservation.Devices);

			GetLegacyReservationResponse Response = new GetLegacyReservationResponse();

			Response.Guid = Reservation.LegacyGuid;
			Response.DeviceNames = Devices.Select(x => x.Name).ToArray();
			Response.DevicePerfSpecs = PerfSpecs.ToArray();
			Response.HostName = Request.Hostname;
			Response.StartDateTime = Reservation.CreateTimeUtc.ToString("O", System.Globalization.CultureInfo.InvariantCulture);
			Response.Duration = $"{Request.Duration}";

			return new JsonResult(Response, new JsonSerializerOptions() { PropertyNamingPolicy = null });

		}

		/// <summary>
		/// Renew a reservation
		/// </summary>
		[HttpPut]
		[Route("/api/v1/reservations/{ReservationGuid}")]
		[ProducesResponseType(typeof(GetLegacyReservationResponse), 200)]
		public async Task<ActionResult<GetLegacyReservationResponse>> UpdateReservationV1Async(string ReservationGuid /* [FromBody] string Duration */)
		{
			IDeviceReservation? Reservation = await DeviceService.TryGetReservationFromLegacyGuidAsync(ReservationGuid);

			if (Reservation == null)
			{
				Logger.LogError($"Unable to find reservation for legacy guid {ReservationGuid}");
				return BadRequest();
			}

			bool Updated = await DeviceService.TryUpdateReservationAsync(Reservation.Id);

			if (!Updated)
			{
				Logger.LogError($"Unable to find reservation for reservation {Reservation.Id}");
				return BadRequest();
			}

			List<IDevice> Devices = await DeviceService.GetDevicesAsync(Reservation.Devices);

			GetLegacyReservationResponse Response = new GetLegacyReservationResponse();

			Response.Guid = Reservation.LegacyGuid;
			Response.DeviceNames = Reservation.Devices.Select(DeviceId => Devices.First(D => D.Id == DeviceId).Name).ToArray();
			Response.HostName = Reservation.Hostname ?? "";
			Response.StartDateTime = Reservation.CreateTimeUtc.ToString("O", System.Globalization.CultureInfo.InvariantCulture);
			Response.Duration = "00:10:00"; // matches gauntlet duration

			return new JsonResult(Response, new JsonSerializerOptions() { PropertyNamingPolicy = null });
		}

		/// <summary>
		/// Delete a reservation
		/// </summary>
		[HttpDelete]
		[Route("/api/v1/reservations/{ReservationGuid}")]
		public async Task<ActionResult> DeleteReservationV1Async(string ReservationGuid)
		{
			IDeviceReservation? Reservation = await DeviceService.TryGetReservationFromLegacyGuidAsync(ReservationGuid);

			if (Reservation == null)
			{
				return BadRequest($"Unable to find reservation for guid {ReservationGuid}");
			}

			bool Deleted = await DeviceService.DeleteReservationAsync(Reservation.Id);

			if (!Deleted)
			{
				return BadRequest("Failed to delete reservation");
			}

			return Ok();
		}


		/// <summary>
		/// Get device info for a reserved device
		/// </summary>
		[HttpGet]
		[Route("/api/v1/devices/{DeviceName}")]
		[ProducesResponseType(typeof(GetLegacyDeviceResponse), 200)]
		public async Task<ActionResult<GetLegacyDeviceResponse>> GetDeviceV1Async(string DeviceName)
		{
			IDevice? Device = await DeviceService.GetDeviceByNameAsync(DeviceName);

			if (Device == null)
			{
				return BadRequest($"Unknown device {DeviceName}");
			}

			DevicePlatformMapV1 MapV1 = await DeviceService.GetPlatformMapV1();

			string? Platform;
			if (!MapV1.PlatformReverseMap.TryGetValue(Device.PlatformId, out Platform))
			{
				return BadRequest($"Unable to map platform for {DeviceName} : {Device.PlatformId}");
			}

			GetLegacyDeviceResponse Response = new GetLegacyDeviceResponse();

			Response.Name = Device.Name;
			Response.Type = Platform;
			Response.IPOrHostName = Device.Address ?? "";
			Response.AvailableStartTime = "00:00:00";
			Response.AvailableEndTime = "00:00:00";
			Response.Enabled = true;
			Response.DeviceData = "";

			Response.PerfSpec = "Minimum";

			if (Device.ModelId != null)
			{
				string? Model = null;
				if (MapV1.PerfSpecHighMap.TryGetValue(Device.PlatformId, out Model))
				{
					if (Model == Device.ModelId)
					{
						Response.PerfSpec = "High";
					}

				}
			}

			return new JsonResult(Response, new JsonSerializerOptions() { PropertyNamingPolicy = null });

		}

		/// <summary>
		/// Mark a problem device
		/// </summary>
		[HttpPut]
		[Route("/api/v1/deviceerror/{DeviceName}")]
		public async Task<ActionResult> PutDeviceErrorAsync(string DeviceName)
		{

			IDevice? Device = await DeviceService.GetDeviceByNameAsync(DeviceName);

			if (Device == null)
			{
				Logger.LogError($"Device error reported for unknown device {DeviceName}");
				return BadRequest($"Unknown device {DeviceName}");
			}

			await DeviceService.UpdateDeviceAsync(Device.Id, NewProblem: true);

			string Message = $"Device problem, {Device.Name} : {Device.PoolId.ToString().ToUpperInvariant()}";

			IDeviceReservation? Reservation = await DeviceService.TryGetDeviceReservation(Device.Id);

			string? JobId = null;
			string? StepId = null;
			if (Reservation != null)
			{
				JobId = !string.IsNullOrEmpty(Reservation.JobId) ? Reservation.JobId : null;
				StepId = !string.IsNullOrEmpty(Reservation.StepId) ? Reservation.StepId : null;

				if ((JobId == null || StepId == null))
				{
					if (!string.IsNullOrEmpty(Reservation.ReservationDetails))
					{
						Message += $" - {Reservation.ReservationDetails}";
					}
					else
					{
						Message += $" - Host {Reservation.Hostname}";
					}
				}
			}

			Logger.LogError(Message);

			await DeviceService.NotifyDeviceServiceAsync(Message, Device.Id, JobId, StepId);

			return Ok();

		}

		/// <summary>
		/// Updates the platform map for v1 requests
		/// </summary>
		[HttpPut]
		[Route("/api/v1/devices/platformmap")]
		public async Task<ActionResult> UpdatePlatformMapV1([FromBody] UpdatePlatformMapRequest Request)
		{
			if (!await AclService.AuthorizeAsync(AclAction.AdminWrite, User))
			{
				return Forbid();
			}

			bool Result = false;

			try
			{
				Result = await DeviceService.UpdatePlatformMapAsync(Request);
			}
			catch (Exception Ex)
			{
				Logger.LogError(Ex, "Error updating device platform map {Message}", Ex.Message);
				throw;
			}

			if (!Result)
			{
				Logger.LogError("Unable to update device platform mapping");
				return BadRequest();
			}

			return Ok();
		}
	}
}