// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Primitives;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Reflection;
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
			public string[]? FileMatch { get; set; }
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
			foreach (Type SchemaType in Program.ConfigSchemas)
			{
				JsonSchemaAttribute? SchemaAttribute = SchemaType.GetCustomAttribute<JsonSchemaAttribute>();
				if (SchemaAttribute != null)
				{
					JsonSchemaCatalogAttribute? CatalogAttribute = SchemaType.GetCustomAttribute<JsonSchemaCatalogAttribute>();
					if (CatalogAttribute != null)
					{
						Uri Url = new Uri($"https://{Host}/api/v1/schema/types/{SchemaType.Name}.json");
						Root.Schemas.Add(new CatalogItem { Name = CatalogAttribute.Name, Description = CatalogAttribute.Description, FileMatch = CatalogAttribute.FileMatch, Url = Url });
					}
				}
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
			foreach (Type SchemaType in Program.ConfigSchemas)
			{
				if (SchemaType.Name.Equals(TypeName, StringComparison.OrdinalIgnoreCase))
				{
					JsonSchema Schema = Schemas.CreateSchema(SchemaType);

					using MemoryStream Stream = new MemoryStream();
					Schema.Write(Stream);

					return new FileContentResult(Stream.ToArray(), "application/json");
				}
			}
			return NotFound();
		}
	}
}
