// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Perforce;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Diagnostics.CodeAnalysis;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

#nullable enable

namespace UnrealGameSync
{
	partial class SelectStreamWindow : Form
	{
		static class EnumerateStreamsTask
		{
			public static async Task<List<StreamsRecord>> RunAsync(IPerforceConnection Perforce, CancellationToken CancellationToken)
			{
				return await Perforce.GetStreamsAsync(null, CancellationToken);
			}
		}

		class StreamNode
		{
			public StreamsRecord Record;
			public List<StreamNode> ChildNodes = new List<StreamNode>();

			public StreamNode(StreamsRecord Record)
			{
				this.Record = Record;
			}

			public void Sort()
			{
				ChildNodes = ChildNodes.OrderBy(x => x.Record.Name).ToList();
				foreach(StreamNode ChildNode in ChildNodes)
				{
					ChildNode.Sort();
				}
			}
		}

		class StreamDepot
		{
			public string Name;
			public List<StreamNode> RootNodes = new List<StreamNode>();

			public StreamDepot(string Name)
			{
				this.Name = Name;
			}

			public void Sort()
			{
				RootNodes = RootNodes.OrderBy(x => x.Record.Name).ToList();
				foreach(StreamNode RootNode in RootNodes)
				{
					RootNode.Sort();
				}
			}
		}

		private string? SelectedStream;
		private List<StreamDepot> Depots = new List<StreamDepot>();

		private SelectStreamWindow(List<StreamsRecord> Streams, string? StreamName)
		{
			InitializeComponent();

			this.SelectedStream = StreamName;

			// Set up the image list
			ImageList PerforceImageList = new ImageList();
			PerforceImageList.ImageSize = new Size(16, 16);
			PerforceImageList.ColorDepth = ColorDepth.Depth32Bit;
			PerforceImageList.Images.AddStrip(Properties.Resources.Perforce);
			StreamsTreeView.ImageList = PerforceImageList;

			// Build a map of stream names to their nodes
			Dictionary<string, StreamNode> IdentifierToNode = new Dictionary<string, StreamNode>(StringComparer.InvariantCultureIgnoreCase);
			foreach(StreamsRecord Stream in Streams)
			{
				if(Stream.Stream != null && Stream.Name != null)
				{
					IdentifierToNode[Stream.Stream] = new StreamNode(Stream);
				}
			}

			// Create all the depots
			Dictionary<string, StreamDepot> NameToDepot = new Dictionary<string, StreamDepot>(StringComparer.InvariantCultureIgnoreCase);
			foreach(StreamNode Node in IdentifierToNode.Values)
			{
				if(Node.Record.Parent == null || Node.Record.Parent.Equals("none", StringComparison.OrdinalIgnoreCase))
				{
					string? DepotName;
					if(PerforceUtils.TryGetDepotName(Node.Record.Stream, out DepotName))
					{
						StreamDepot? Depot;
						if(!NameToDepot.TryGetValue(DepotName, out Depot))
						{
							Depot = new StreamDepot(DepotName);
							NameToDepot.Add(DepotName, Depot);
						}
						Depot.RootNodes.Add(Node);
					}
				}
				else
				{
					StreamNode? ParentNode;
					if(IdentifierToNode.TryGetValue(Node.Record.Parent, out ParentNode))
					{
						ParentNode.ChildNodes.Add(Node);
					}
				}
			}

			// Sort the tree
			Depots = NameToDepot.Values.OrderBy(x => x.Name).ToList();
			foreach(StreamDepot Depot in Depots)
			{
				Depot.Sort();
			}

			// Update the contents of the tree
			PopulateTree();
			UpdateOkButton();
		}

		private void GetExpandedNodes(TreeNodeCollection Nodes, List<TreeNode> ExpandedNodes)
		{
			foreach(TreeNode? Node in Nodes)
			{
				if (Node != null)
				{
					ExpandedNodes.Add(Node);
					if (Node.IsExpanded)
					{
						GetExpandedNodes(Node.Nodes, ExpandedNodes);
					}
				}
			}
		}

		private void MoveSelection(int Delta)
		{
			if(StreamsTreeView.SelectedNode != null)
			{
				List<TreeNode> ExpandedNodes = new List<TreeNode>();
				GetExpandedNodes(StreamsTreeView.Nodes, ExpandedNodes);

				int Idx = ExpandedNodes.IndexOf(StreamsTreeView.SelectedNode);
				if(Idx != -1)
				{
					int NextIdx = Idx + Delta;
					if(NextIdx < 0)
					{
						NextIdx = 0;
					}
					if(NextIdx >= ExpandedNodes.Count)
					{
						NextIdx = ExpandedNodes.Count - 1;
					}
					StreamsTreeView.SelectedNode = ExpandedNodes[NextIdx];
				}
			}
		}

		protected override bool ProcessCmdKey(ref Message Msg, Keys KeyData)
		{
			if(KeyData == Keys.Up)
			{
				MoveSelection(-1);
				return true;
			}
			else if(KeyData == Keys.Down)
			{
				MoveSelection(+1);
				return true;
			}
			return base.ProcessCmdKey(ref Msg, KeyData);
		}

		private bool IncludeNodeInFilter(StreamNode Node, string[] Filter)
		{
			return Filter.All(x => Node.Record.Stream.IndexOf(x, StringComparison.InvariantCultureIgnoreCase) != -1 || Node.Record.Name.IndexOf(x, StringComparison.InvariantCultureIgnoreCase) != -1);
		}

		private bool TryFilterTree(StreamNode Node, string[] Filter, [NotNullWhen(true)] out StreamNode? NewNode)
		{
			StreamNode FilteredNode = new StreamNode(Node.Record);
			foreach(StreamNode ChildNode in Node.ChildNodes)
			{
				StreamNode? FilteredChildNode;
				if(TryFilterTree(ChildNode, Filter, out FilteredChildNode))
				{
					FilteredNode.ChildNodes.Add(FilteredChildNode);
				}
			}

			if(FilteredNode.ChildNodes.Count > 0 || IncludeNodeInFilter(FilteredNode, Filter))
			{
				NewNode = FilteredNode;
				return true;
			}
			else
			{
				NewNode = null;
				return false;
			}
		}

		private void PopulateTree()
		{
			StreamsTreeView.BeginUpdate();
			StreamsTreeView.Nodes.Clear();

			string[] Filter = FilterTextBox.Text.Split(new char[]{' '}, StringSplitOptions.RemoveEmptyEntries);

			List<StreamDepot> FilteredDepots = Depots;
			if(Filter.Length > 0)
			{
				FilteredDepots = new List<StreamDepot>();
				foreach(StreamDepot Depot in Depots)
				{
					StreamDepot FilteredDepot = new StreamDepot(Depot.Name);
					foreach(StreamNode RootNode in Depot.RootNodes)
					{
						StreamNode? FilteredRootNode;
						if(TryFilterTree(RootNode, Filter, out FilteredRootNode))
						{
							FilteredDepot.RootNodes.Add(FilteredRootNode);
						}
					}
					if(FilteredDepot.RootNodes.Count > 0)
					{
						FilteredDepots.Add(FilteredDepot);
					}
				}
			}

			bool bExpandAll = Filter.Length > 0;
			foreach(StreamDepot Depot in FilteredDepots)
			{
				TreeNode DepotTreeNode = new TreeNode(Depot.Name);
				DepotTreeNode.ImageIndex = 1;
				DepotTreeNode.SelectedImageIndex = 1;
				StreamsTreeView.Nodes.Add(DepotTreeNode);

				bool bExpand = bExpandAll;
				foreach(StreamNode RootNode in Depot.RootNodes)
				{
					bExpand |= AddStreamNodeToTree(RootNode, Filter, DepotTreeNode, bExpandAll);
				}
				if(bExpand)
				{
					DepotTreeNode.Expand();
				}
			}

			if(StreamsTreeView.SelectedNode == null && Filter.Length > 0 && StreamsTreeView.Nodes.Count > 0)
			{
				for(TreeNode Node = StreamsTreeView.Nodes[0];;Node = Node.Nodes[0])
				{
					StreamNode? Stream = Node.Tag as StreamNode;
					if(Stream != null && IncludeNodeInFilter(Stream, Filter))
					{
						StreamsTreeView.SelectedNode = Node;
						break;
					}
					if(Node.Nodes.Count == 0)
					{
						break;
					}
				}
			}

			if(StreamsTreeView.SelectedNode != null)
			{
				StreamsTreeView.SelectedNode.EnsureVisible();
			}
			else if(StreamsTreeView.Nodes.Count > 0)
			{
				StreamsTreeView.Nodes[0].EnsureVisible();
			}
			StreamsTreeView.EndUpdate();

			UpdateOkButton();
		}

		private bool AddStreamNodeToTree(StreamNode Stream, string[] Filter, TreeNode ParentTreeNode, bool bExpandAll)
		{
			TreeNode StreamTreeNode = new TreeNode(Stream.Record.Name);
			StreamTreeNode.ImageIndex = 0;
			StreamTreeNode.SelectedImageIndex = 0;
			StreamTreeNode.Tag = Stream;
			ParentTreeNode.Nodes.Add(StreamTreeNode);

			if(Stream.Record.Name == SelectedStream && IncludeNodeInFilter(Stream, Filter))
			{
				StreamsTreeView.SelectedNode = StreamTreeNode;
			}

			bool bExpand = bExpandAll;
			foreach(StreamNode ChildNode in Stream.ChildNodes)
			{
				bExpand |= AddStreamNodeToTree(ChildNode, Filter, StreamTreeNode, bExpandAll);
			}
			if(bExpand)
			{
				StreamTreeNode.Expand();
			}
			return bExpand || (SelectedStream == Stream.Record.Stream);
		}

		public static bool ShowModal(IWin32Window Owner, IPerforceSettings Perforce, string? StreamName, IServiceProvider ServiceProvider, [NotNullWhen(true)] out string? NewStreamName)
		{
			ILogger Logger = ServiceProvider.GetRequiredService<ILogger<SelectStreamWindow>>();

			ModalTask<List<StreamsRecord>>? StreamsTask = PerforceModalTask.Execute(Owner, "Finding streams", "Finding streams, please wait...", Perforce, EnumerateStreamsTask.RunAsync, Logger);
			if(StreamsTask == null || !StreamsTask.Succeeded)
			{
				NewStreamName = null;
				return false;
			}

			SelectStreamWindow SelectStream = new SelectStreamWindow(StreamsTask.Result, StreamName);
			if(SelectStream.ShowDialog(Owner) == DialogResult.OK && SelectStream.SelectedStream != null)
			{
				NewStreamName = SelectStream.SelectedStream;
				return true;
			}
			else
			{
				NewStreamName = null;
				return false;
			}
		}

		private void FilterTextBox_TextChanged(object sender, EventArgs e)
		{
			PopulateTree();
		}

		private void UpdateOkButton()
		{
			OkBtn.Enabled = (GetSelectedStream() != null);
		}

		private string? GetSelectedStream()
		{
			string? NewSelectedStream = null;
			if(StreamsTreeView.SelectedNode != null)
			{
				StreamNode StreamNode = (StreamNode)StreamsTreeView.SelectedNode.Tag;
				if(StreamNode != null)
				{
					NewSelectedStream = StreamNode.Record.Stream;
				}
			}
			return NewSelectedStream;
		}

		private void UpdateSelectedStream()
		{
			SelectedStream = GetSelectedStream();
		}

		private void StreamsTreeView_AfterSelect(object sender, TreeViewEventArgs e)
		{
			if(e.Action != TreeViewAction.Unknown)
			{
				UpdateSelectedStream();
			}
			UpdateOkButton();
		}

		private void OkBtn_Click(object sender, EventArgs e)
		{
			UpdateSelectedStream();

			if(SelectedStream != null)
			{
				DialogResult = DialogResult.OK;
				Close();
			}
		}

		private void CancelBtn_Click(object sender, EventArgs e)
		{
			DialogResult = DialogResult.Cancel;
			Close();
		}
	}
}
