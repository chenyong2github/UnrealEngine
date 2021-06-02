// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using HordeServer.Utilities;
using Json.Schema;
using Json.Schema.Generation;

using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Primitives;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading.Tasks;

namespace HordeServer.Controllers
{
	/// <summary>
	/// Controller for the /api/v1/schema endpoint
	/// </summary>
	[ApiController]
	[Route("[controller]")]
	public class SchemaController : ControllerBase
	{
		class CatalogItem
		{
			public string? Name { get; set; }
			public string? Description { get; set; }
			public Uri? Url { get; set; }
		}

		class CatalogRoot
		{
			[JsonPropertyName("$schema")]
			public string Schema { get; set; } = "https://json.schemastore.org/schema-catalog.json";
			public int Version { get; set; } = 1;
			public List<CatalogItem> Schemas { get; set; } = new List<CatalogItem>();
		}

		/// <summary>
		/// Get the catalog for config schema
		/// </summary>
		/// <returns>Information about all the schedules</returns>
		[HttpGet]
		[Route("/api/v1/schema/catalog.json")]
		public ActionResult GetCatalog()
		{
			string? Host = null;

			StringValues Hosts;
			if (Request.Headers.TryGetValue("Host", out Hosts))
			{
				Host = Hosts.FirstOrDefault();
			}

			if (Host == null)
			{
				Host = Dns.GetHostName();
			}

			CatalogRoot Root = new CatalogRoot();
			foreach (SchemaInfo Schema in Program.ConfigSchemas)
			{
				Uri Url = new Uri($"https://{Host}/api/v1/schema/types/{Schema.Type.Name}.json");
				Root.Schemas.Add(new CatalogItem { Name = Schema.Name, Description = Schema.Description, Url = Url });
			}
			return Ok(Root);
		}

		/// <summary>
		/// Gets a specific schema
		/// </summary>
		/// <param name="TypeName">The type name</param>
		/// <returns></returns>
		[HttpGet]
		[Route("/api/v1/schema/types/{TypeName}.json")]
		public ActionResult GetSchema(string TypeName)
		{
			foreach (SchemaInfo Schema in Program.ConfigSchemas)
			{
				if (Schema.Type.Name.Equals(TypeName, StringComparison.OrdinalIgnoreCase))
				{
					return Ok(Schemas.CreateSchema(Schema.Id, Schema.Type));
				}
			}
			return NotFound();
		}
	}
}
