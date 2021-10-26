// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Api;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Services;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace HordeServer.Collections
{
	/// <summary>
	/// Interface for a collection of graph documents
	/// </summary>
	public interface IGraphCollection
	{
		/// <summary>
		/// Adds a graph from a template
		/// </summary>
		/// <param name="Template">The template</param>
		/// <returns>New graph</returns>
		Task<IGraph> AddAsync(ITemplate Template);

		/// <summary>
		/// Creates a graph by appending groups and aggregates to an existing graph.
		/// </summary>
		/// <param name="BaseGraph">The base graph</param>
		/// <param name="NewGroupRequests">List of group requests</param>
		/// <param name="NewAggregateRequests">List of aggregate requests</param>
		/// <param name="NewLabelRequests">List of label requests</param>
		/// <returns>The new graph definition</returns>
		Task<IGraph> AppendAsync(IGraph? BaseGraph, List<NewGroup>? NewGroupRequests = null, List<NewAggregate>? NewAggregateRequests = null, List<NewLabel>? NewLabelRequests = null);

		/// <summary>
		/// Gets the graph for a job
		/// </summary>
		/// <param name="Hash">Hash of the graph to retrieve</param>
		/// <returns>The graph for this job</returns>
		Task<IGraph> GetAsync(ContentHash Hash);

		/// <summary>
		/// Finds all graphs stored in the collection
		/// </summary>
		/// <param name="Hashes">Hashes to filter by</param>
		/// <param name="Index">Starting index of the graph to return</param>
		/// <param name="Count">Number of results to return</param>
		/// <returns>List of graphs</returns>
		Task<List<IGraph>> FindAllAsync(ContentHash[]? Hashes = null, int? Index = null, int? Count = null);
	}
}
