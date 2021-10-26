// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeCommon;
using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Collections
{
	/// <summary>
	/// Interface for a collection of template documents
	/// </summary>
	public interface ITemplateCollection
	{
		/// <summary>
		/// Public constructor
		/// </summary>
		/// <param name="Name">Name of the template</param>
		/// <param name="Priority">Priority of this template</param>
		/// <param name="bAllowPreflights">Whether to allow preflights of this job</param>
		/// <param name="InitialAgentType">The agent type to parse the buildgraph script</param>
		/// <param name="SubmitNewChange">Path to a file within the stream to submit to generate a new changelist for jobs</param>
		/// <param name="Arguments">List of arguments which are always specified</param>
		/// <param name="Parameters">List of template parameters</param>
		Task<ITemplate> AddAsync(string Name, Priority? Priority = null, bool bAllowPreflights = true, string? InitialAgentType = null, string? SubmitNewChange = null, List<string>? Arguments = null, List<Parameter>? Parameters = null);

		/// <summary>
		/// Gets all the available templates
		/// </summary>
		/// <returns>List of template documents</returns>
		Task<List<ITemplate>> FindAllAsync();

		/// <summary>
		/// Gets a template by ID
		/// </summary>
		/// <param name="TemplateId">Unique id of the template</param>
		/// <returns>The template document</returns>
		Task<ITemplate?> GetAsync(ContentHash TemplateId);
	}
}
