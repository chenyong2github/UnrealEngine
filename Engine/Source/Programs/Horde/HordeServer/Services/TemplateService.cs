// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Api;
using HordeServer.Collections;
using HordeCommon;
using HordeServer.Models;
using MongoDB.Bson;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Security.Claims;
using System.Threading.Tasks;

namespace HordeServer.Services
{
	/// <summary>
	/// Wraps functionality for manipulating job templates
	/// </summary>
	public class TemplateService
	{
		/// <summary>
		/// Collection of template documents
		/// </summary>
		ITemplateCollection Templates;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Templates">Collection of template documents</param>
		public TemplateService(ITemplateCollection Templates)
		{
			this.Templates = Templates;
		}

		/// <summary>
		/// Creates a new template
		/// </summary>
		/// <param name="Name">Name of the new template</param>
		/// <param name="Priority">Priority for the new template</param>
		/// <param name="AllowPreflights">Whether to allow preflights of this template</param>
		/// <param name="InitialAgentType">The initial agent type to parse the BuildGraph script</param>
		/// <param name="SubmitNewChange">Path to a file within the stream to submit to generate a new changelist for jobs</param>
		/// <param name="Counters">List of counters for the template</param>
		/// <param name="Arguments">Common arguments for jobs started from this template</param>
		/// <param name="Parameters">List of parameters for this template</param>
		/// <returns>The new template document</returns>
		public Task<ITemplate> CreateTemplateAsync(string Name, Priority? Priority = null, bool AllowPreflights = true, string? InitialAgentType = null, string? SubmitNewChange = null, List<TemplateCounter>? Counters = null, List<string>? Arguments = null, List<Parameter>? Parameters = null)
		{
			return Templates.AddAsync(Name, Priority, AllowPreflights, InitialAgentType, SubmitNewChange, Counters, Arguments, Parameters);
		}

		/// <summary>
		/// Gets all the available templates
		/// </summary>
		/// <returns>List of template documents</returns>
		public Task<List<ITemplate>> GetTemplatesAsync()
		{
			return Templates.FindAllAsync();
		}

		/// <summary>
		/// Gets a template by ID
		/// </summary>
		/// <param name="TemplateId">Unique id of the template</param>
		/// <returns>The template document</returns>
		public Task<ITemplate?> GetTemplateAsync(ContentHash TemplateId)
		{
			return Templates.GetAsync(TemplateId);
		}
	}
}
