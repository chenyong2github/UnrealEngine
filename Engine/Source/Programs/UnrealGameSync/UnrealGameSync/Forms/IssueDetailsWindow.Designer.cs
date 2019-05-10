// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealGameSync
{
	partial class IssueDetailsWindow
	{
		/// <summary>
		/// Required designer variable.
		/// </summary>
		private System.ComponentModel.IContainer components = null;

		/// <summary>
		/// Clean up any resources being used.
		/// </summary>
		/// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
		protected override void Dispose(bool disposing)
		{
			if (disposing && (components != null))
			{
				components.Dispose();
			}
			base.Dispose(disposing);
		}

		#region Windows Form Designer generated code

		/// <summary>
		/// Required method for Designer support - do not modify
		/// the contents of this method with the code editor.
		/// </summary>
		private void InitializeComponent()
		{
            this.components = new System.ComponentModel.Container();
            this.StreamComboBox = new System.Windows.Forms.ComboBox();
            this.OkBtn = new System.Windows.Forms.Button();
            this.AssignToOtherBtn = new System.Windows.Forms.Button();
            this.BuildListContextMenu = new System.Windows.Forms.ContextMenuStrip(this.components);
            this.BuildListContextMenu_Assign = new System.Windows.Forms.ToolStripMenuItem();
            this.toolStripSeparator1 = new System.Windows.Forms.ToolStripSeparator();
            this.BuildListContextMenu_MoreInfo = new System.Windows.Forms.ToolStripMenuItem();
            this.OpenSinceTextBox = new System.Windows.Forms.TextBox();
            this.AssignBtn = new System.Windows.Forms.Button();
            this.label1 = new System.Windows.Forms.Label();
            this.label3 = new System.Windows.Forms.Label();
            this.label5 = new System.Windows.Forms.Label();
            this.StatusTextBox = new System.Windows.Forms.TextBox();
            this.tableLayoutPanel1 = new System.Windows.Forms.TableLayoutPanel();
            this.StreamNamesTextBox = new System.Windows.Forms.TextBox();
            this.label4 = new System.Windows.Forms.Label();
            this.StepNamesTextBox = new System.Windows.Forms.TextBox();
            this.label2 = new System.Windows.Forms.Label();
            this.SummaryTextBox = new System.Windows.Forms.TextBox();
            this.groupBox1 = new System.Windows.Forms.GroupBox();
            this.tableLayoutPanel2 = new System.Windows.Forms.TableLayoutPanel();
            this.tableLayoutPanel3 = new System.Windows.Forms.TableLayoutPanel();
            this.FilterTextBox = new UnrealGameSync.TextBoxWithCueBanner();
            this.FilterTypeComboBox = new System.Windows.Forms.ComboBox();
            this.BuildListView = new UnrealGameSync.CustomListViewControl();
            this.IconHeader = ((System.Windows.Forms.ColumnHeader)(new System.Windows.Forms.ColumnHeader()));
            this.ChangeHeader = ((System.Windows.Forms.ColumnHeader)(new System.Windows.Forms.ColumnHeader()));
            this.TypeHeader = ((System.Windows.Forms.ColumnHeader)(new System.Windows.Forms.ColumnHeader()));
            this.AuthorHeader = ((System.Windows.Forms.ColumnHeader)(new System.Windows.Forms.ColumnHeader()));
            this.DescriptionHeader = ((System.Windows.Forms.ColumnHeader)(new System.Windows.Forms.ColumnHeader()));
            this.AssignToMeBtn = new System.Windows.Forms.Button();
            this.MarkFixedBtn = new System.Windows.Forms.Button();
            this.ReopenBtn = new System.Windows.Forms.Button();
            this.tableLayoutPanel4 = new System.Windows.Forms.TableLayoutPanel();
            this.splitContainer1 = new System.Windows.Forms.SplitContainer();
            this.panel1 = new System.Windows.Forms.Panel();
            this.DetailsTextBox = new System.Windows.Forms.TextBox();
            this.tableLayoutPanel5 = new System.Windows.Forms.TableLayoutPanel();
            this.JobContextMenu = new System.Windows.Forms.ContextMenuStrip(this.components);
            this.JobContextMenu_ShowFirstError = new System.Windows.Forms.ToolStripMenuItem();
            this.JobContextMenu_StepSeparatorMin = new System.Windows.Forms.ToolStripSeparator();
            this.toolStripMenuItem1 = new System.Windows.Forms.ToolStripMenuItem();
            this.JobContextMenu_StepSeparatorMax = new System.Windows.Forms.ToolStripSeparator();
            this.JobContextMenu_ViewJob = new System.Windows.Forms.ToolStripMenuItem();
            this.BuildListContextMenu.SuspendLayout();
            this.tableLayoutPanel1.SuspendLayout();
            this.groupBox1.SuspendLayout();
            this.tableLayoutPanel2.SuspendLayout();
            this.tableLayoutPanel3.SuspendLayout();
            this.tableLayoutPanel4.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)(this.splitContainer1)).BeginInit();
            this.splitContainer1.Panel1.SuspendLayout();
            this.splitContainer1.Panel2.SuspendLayout();
            this.splitContainer1.SuspendLayout();
            this.panel1.SuspendLayout();
            this.tableLayoutPanel5.SuspendLayout();
            this.JobContextMenu.SuspendLayout();
            this.SuspendLayout();
            // 
            // StreamComboBox
            // 
            this.StreamComboBox.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.StreamComboBox.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
            this.StreamComboBox.FormattingEnabled = true;
            this.StreamComboBox.Location = new System.Drawing.Point(3, 3);
            this.StreamComboBox.Name = "StreamComboBox";
            this.StreamComboBox.Size = new System.Drawing.Size(1131, 23);
            this.StreamComboBox.TabIndex = 0;
            this.StreamComboBox.SelectedIndexChanged += new System.EventHandler(this.StreamComboBox_SelectedIndexChanged);
            // 
            // OkBtn
            // 
            this.OkBtn.Anchor = System.Windows.Forms.AnchorStyles.None;
            this.OkBtn.DialogResult = System.Windows.Forms.DialogResult.Cancel;
            this.OkBtn.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F);
            this.OkBtn.Location = new System.Drawing.Point(1065, 3);
            this.OkBtn.Name = "OkBtn";
            this.OkBtn.Size = new System.Drawing.Size(97, 27);
            this.OkBtn.TabIndex = 4;
            this.OkBtn.Text = "Close";
            this.OkBtn.UseVisualStyleBackColor = true;
            this.OkBtn.Click += new System.EventHandler(this.CloseBtn_Click);
            // 
            // AssignToOtherBtn
            // 
            this.AssignToOtherBtn.Anchor = System.Windows.Forms.AnchorStyles.None;
            this.AssignToOtherBtn.Location = new System.Drawing.Point(273, 3);
            this.AssignToOtherBtn.Name = "AssignToOtherBtn";
            this.AssignToOtherBtn.Size = new System.Drawing.Size(129, 27);
            this.AssignToOtherBtn.TabIndex = 2;
            this.AssignToOtherBtn.Text = "Assign To Other...";
            this.AssignToOtherBtn.UseVisualStyleBackColor = true;
            this.AssignToOtherBtn.Click += new System.EventHandler(this.AssignToOtherBtn_Click);
            // 
            // BuildListContextMenu
            // 
            this.BuildListContextMenu.Items.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.BuildListContextMenu_Assign,
            this.toolStripSeparator1,
            this.BuildListContextMenu_MoreInfo});
            this.BuildListContextMenu.Name = "ContextMenu";
            this.BuildListContextMenu.Size = new System.Drawing.Size(136, 54);
            // 
            // BuildListContextMenu_Assign
            // 
            this.BuildListContextMenu_Assign.Name = "BuildListContextMenu_Assign";
            this.BuildListContextMenu_Assign.Size = new System.Drawing.Size(135, 22);
            this.BuildListContextMenu_Assign.Text = "Assign";
            this.BuildListContextMenu_Assign.Click += new System.EventHandler(this.BuildListContextMenu_Blame_Click);
            // 
            // toolStripSeparator1
            // 
            this.toolStripSeparator1.Name = "toolStripSeparator1";
            this.toolStripSeparator1.Size = new System.Drawing.Size(132, 6);
            // 
            // BuildListContextMenu_MoreInfo
            // 
            this.BuildListContextMenu_MoreInfo.Name = "BuildListContextMenu_MoreInfo";
            this.BuildListContextMenu_MoreInfo.Size = new System.Drawing.Size(135, 22);
            this.BuildListContextMenu_MoreInfo.Text = "More Info...";
            this.BuildListContextMenu_MoreInfo.Click += new System.EventHandler(this.BuildListContextMenu_MoreInfo_Click);
            // 
            // OpenSinceTextBox
            // 
            this.OpenSinceTextBox.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.OpenSinceTextBox.BorderStyle = System.Windows.Forms.BorderStyle.None;
            this.OpenSinceTextBox.Location = new System.Drawing.Point(104, 47);
            this.OpenSinceTextBox.Name = "OpenSinceTextBox";
            this.OpenSinceTextBox.ReadOnly = true;
            this.OpenSinceTextBox.Size = new System.Drawing.Size(1058, 16);
            this.OpenSinceTextBox.TabIndex = 5;
            this.OpenSinceTextBox.Text = "1/23/2019 (3 minutes)";
            // 
            // AssignBtn
            // 
            this.AssignBtn.Anchor = System.Windows.Forms.AnchorStyles.None;
            this.AssignBtn.Location = new System.Drawing.Point(3, 3);
            this.AssignBtn.Name = "AssignBtn";
            this.AssignBtn.Size = new System.Drawing.Size(129, 27);
            this.AssignBtn.TabIndex = 0;
            this.AssignBtn.Text = "Assign";
            this.AssignBtn.UseVisualStyleBackColor = true;
            this.AssignBtn.Click += new System.EventHandler(this.AssignBtn_Click);
            // 
            // label1
            // 
            this.label1.AutoSize = true;
            this.label1.Location = new System.Drawing.Point(3, 3);
            this.label1.Margin = new System.Windows.Forms.Padding(3);
            this.label1.Name = "label1";
            this.label1.Padding = new System.Windows.Forms.Padding(0, 0, 25, 0);
            this.label1.Size = new System.Drawing.Size(86, 15);
            this.label1.TabIndex = 0;
            this.label1.Text = "Summary:";
            // 
            // label3
            // 
            this.label3.AutoSize = true;
            this.label3.Location = new System.Drawing.Point(3, 47);
            this.label3.Margin = new System.Windows.Forms.Padding(3);
            this.label3.Name = "label3";
            this.label3.Padding = new System.Windows.Forms.Padding(0, 0, 25, 0);
            this.label3.Size = new System.Drawing.Size(95, 15);
            this.label3.TabIndex = 4;
            this.label3.Text = "Open Since:";
            // 
            // label5
            // 
            this.label5.AutoSize = true;
            this.label5.Location = new System.Drawing.Point(3, 25);
            this.label5.Margin = new System.Windows.Forms.Padding(3);
            this.label5.Name = "label5";
            this.label5.Padding = new System.Windows.Forms.Padding(0, 0, 25, 0);
            this.label5.Size = new System.Drawing.Size(67, 15);
            this.label5.TabIndex = 2;
            this.label5.Text = "Status:";
            // 
            // StatusTextBox
            // 
            this.StatusTextBox.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.StatusTextBox.BorderStyle = System.Windows.Forms.BorderStyle.None;
            this.StatusTextBox.Location = new System.Drawing.Point(104, 25);
            this.StatusTextBox.Name = "StatusTextBox";
            this.StatusTextBox.ReadOnly = true;
            this.StatusTextBox.Size = new System.Drawing.Size(1058, 16);
            this.StatusTextBox.TabIndex = 3;
            this.StatusTextBox.Text = "Assigned to Ben.Marsh";
            // 
            // tableLayoutPanel1
            // 
            this.tableLayoutPanel1.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.tableLayoutPanel1.AutoSize = true;
            this.tableLayoutPanel1.ColumnCount = 2;
            this.tableLayoutPanel1.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle());
            this.tableLayoutPanel1.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 100F));
            this.tableLayoutPanel1.Controls.Add(this.StreamNamesTextBox, 1, 4);
            this.tableLayoutPanel1.Controls.Add(this.label4, 0, 4);
            this.tableLayoutPanel1.Controls.Add(this.StepNamesTextBox, 1, 3);
            this.tableLayoutPanel1.Controls.Add(this.label1, 0, 0);
            this.tableLayoutPanel1.Controls.Add(this.label3, 0, 2);
            this.tableLayoutPanel1.Controls.Add(this.OpenSinceTextBox, 1, 2);
            this.tableLayoutPanel1.Controls.Add(this.StatusTextBox, 1, 1);
            this.tableLayoutPanel1.Controls.Add(this.label5, 0, 1);
            this.tableLayoutPanel1.Controls.Add(this.label2, 0, 3);
            this.tableLayoutPanel1.Controls.Add(this.SummaryTextBox, 1, 0);
            this.tableLayoutPanel1.Location = new System.Drawing.Point(3, 3);
            this.tableLayoutPanel1.Margin = new System.Windows.Forms.Padding(3, 3, 3, 11);
            this.tableLayoutPanel1.Name = "tableLayoutPanel1";
            this.tableLayoutPanel1.RowCount = 5;
            this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle());
            this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle());
            this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle());
            this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle());
            this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle());
            this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Absolute, 20F));
            this.tableLayoutPanel1.Size = new System.Drawing.Size(1165, 110);
            this.tableLayoutPanel1.TabIndex = 0;
            // 
            // StreamNamesTextBox
            // 
            this.StreamNamesTextBox.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.StreamNamesTextBox.BorderStyle = System.Windows.Forms.BorderStyle.None;
            this.StreamNamesTextBox.Location = new System.Drawing.Point(104, 91);
            this.StreamNamesTextBox.Name = "StreamNamesTextBox";
            this.StreamNamesTextBox.ReadOnly = true;
            this.StreamNamesTextBox.Size = new System.Drawing.Size(1058, 16);
            this.StreamNamesTextBox.TabIndex = 10;
            this.StreamNamesTextBox.Text = "Stream A, Stream B, Stream C";
            // 
            // label4
            // 
            this.label4.AutoSize = true;
            this.label4.Location = new System.Drawing.Point(3, 91);
            this.label4.Margin = new System.Windows.Forms.Padding(3);
            this.label4.Name = "label4";
            this.label4.Size = new System.Drawing.Size(52, 15);
            this.label4.TabIndex = 8;
            this.label4.Text = "Streams:";
            // 
            // StepNamesTextBox
            // 
            this.StepNamesTextBox.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.StepNamesTextBox.BorderStyle = System.Windows.Forms.BorderStyle.None;
            this.StepNamesTextBox.Location = new System.Drawing.Point(104, 69);
            this.StepNamesTextBox.Name = "StepNamesTextBox";
            this.StepNamesTextBox.ReadOnly = true;
            this.StepNamesTextBox.Size = new System.Drawing.Size(1058, 16);
            this.StepNamesTextBox.TabIndex = 7;
            this.StepNamesTextBox.Text = "Step A, Step B, Step C";
            // 
            // label2
            // 
            this.label2.AutoSize = true;
            this.label2.Location = new System.Drawing.Point(3, 69);
            this.label2.Margin = new System.Windows.Forms.Padding(3);
            this.label2.Name = "label2";
            this.label2.Size = new System.Drawing.Size(38, 15);
            this.label2.TabIndex = 6;
            this.label2.Text = "Steps:";
            // 
            // SummaryTextBox
            // 
            this.SummaryTextBox.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.SummaryTextBox.BorderStyle = System.Windows.Forms.BorderStyle.None;
            this.SummaryTextBox.Location = new System.Drawing.Point(104, 3);
            this.SummaryTextBox.Name = "SummaryTextBox";
            this.SummaryTextBox.ReadOnly = true;
            this.SummaryTextBox.Size = new System.Drawing.Size(1058, 16);
            this.SummaryTextBox.TabIndex = 9;
            this.SummaryTextBox.Text = "Issue Summary";
            // 
            // groupBox1
            // 
            this.groupBox1.Controls.Add(this.tableLayoutPanel2);
            this.groupBox1.Dock = System.Windows.Forms.DockStyle.Fill;
            this.groupBox1.Location = new System.Drawing.Point(0, 0);
            this.groupBox1.Margin = new System.Windows.Forms.Padding(3, 11, 3, 11);
            this.groupBox1.Name = "groupBox1";
            this.groupBox1.Size = new System.Drawing.Size(1165, 386);
            this.groupBox1.TabIndex = 0;
            this.groupBox1.TabStop = false;
            this.groupBox1.Text = "History";
            // 
            // tableLayoutPanel2
            // 
            this.tableLayoutPanel2.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.tableLayoutPanel2.ColumnCount = 1;
            this.tableLayoutPanel2.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 100F));
            this.tableLayoutPanel2.Controls.Add(this.tableLayoutPanel3, 0, 2);
            this.tableLayoutPanel2.Controls.Add(this.StreamComboBox, 0, 0);
            this.tableLayoutPanel2.Controls.Add(this.BuildListView, 0, 1);
            this.tableLayoutPanel2.Location = new System.Drawing.Point(15, 22);
            this.tableLayoutPanel2.Margin = new System.Windows.Forms.Padding(0);
            this.tableLayoutPanel2.Name = "tableLayoutPanel2";
            this.tableLayoutPanel2.RowCount = 3;
            this.tableLayoutPanel2.RowStyles.Add(new System.Windows.Forms.RowStyle());
            this.tableLayoutPanel2.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Percent, 100F));
            this.tableLayoutPanel2.RowStyles.Add(new System.Windows.Forms.RowStyle());
            this.tableLayoutPanel2.Size = new System.Drawing.Size(1137, 352);
            this.tableLayoutPanel2.TabIndex = 4;
            // 
            // tableLayoutPanel3
            // 
            this.tableLayoutPanel3.AutoSize = true;
            this.tableLayoutPanel3.ColumnCount = 2;
            this.tableLayoutPanel3.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 100F));
            this.tableLayoutPanel3.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle());
            this.tableLayoutPanel3.Controls.Add(this.FilterTextBox, 0, 0);
            this.tableLayoutPanel3.Controls.Add(this.FilterTypeComboBox, 1, 0);
            this.tableLayoutPanel3.Dock = System.Windows.Forms.DockStyle.Fill;
            this.tableLayoutPanel3.Location = new System.Drawing.Point(0, 323);
            this.tableLayoutPanel3.Margin = new System.Windows.Forms.Padding(0);
            this.tableLayoutPanel3.Name = "tableLayoutPanel3";
            this.tableLayoutPanel3.RowCount = 1;
            this.tableLayoutPanel3.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Percent, 100F));
            this.tableLayoutPanel3.Size = new System.Drawing.Size(1137, 29);
            this.tableLayoutPanel3.TabIndex = 0;
            // 
            // FilterTextBox
            // 
            this.FilterTextBox.CueBanner = "Enter search terms. Use wildcards to match paths of modified files.";
            this.FilterTextBox.Dock = System.Windows.Forms.DockStyle.Fill;
            this.FilterTextBox.Location = new System.Drawing.Point(3, 3);
            this.FilterTextBox.Name = "FilterTextBox";
            this.FilterTextBox.Size = new System.Drawing.Size(1004, 23);
            this.FilterTextBox.TabIndex = 0;
            this.FilterTextBox.TextChanged += new System.EventHandler(this.FilterTextBox_TextChanged);
            // 
            // FilterTypeComboBox
            // 
            this.FilterTypeComboBox.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
            this.FilterTypeComboBox.FormattingEnabled = true;
            this.FilterTypeComboBox.Items.AddRange(new object[] {
            "Code & Content",
            "Code Only",
            "Content Only"});
            this.FilterTypeComboBox.Location = new System.Drawing.Point(1013, 3);
            this.FilterTypeComboBox.Name = "FilterTypeComboBox";
            this.FilterTypeComboBox.Size = new System.Drawing.Size(121, 23);
            this.FilterTypeComboBox.TabIndex = 1;
            this.FilterTypeComboBox.SelectedIndexChanged += new System.EventHandler(this.FilterTypeComboBox_SelectedIndexChanged);
            // 
            // BuildListView
            // 
            this.BuildListView.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.BuildListView.BorderStyle = System.Windows.Forms.BorderStyle.FixedSingle;
            this.BuildListView.Columns.AddRange(new System.Windows.Forms.ColumnHeader[] {
            this.IconHeader,
            this.ChangeHeader,
            this.TypeHeader,
            this.AuthorHeader,
            this.DescriptionHeader});
            this.BuildListView.FullRowSelect = true;
            this.BuildListView.HideSelection = false;
            this.BuildListView.Location = new System.Drawing.Point(3, 33);
            this.BuildListView.Margin = new System.Windows.Forms.Padding(3, 4, 3, 4);
            this.BuildListView.Name = "BuildListView";
            this.BuildListView.OwnerDraw = true;
            this.BuildListView.Size = new System.Drawing.Size(1131, 286);
            this.BuildListView.TabIndex = 1;
            this.BuildListView.UseCompatibleStateImageBehavior = false;
            this.BuildListView.View = System.Windows.Forms.View.Details;
            this.BuildListView.DrawColumnHeader += new System.Windows.Forms.DrawListViewColumnHeaderEventHandler(this.BuildListView_DrawColumnHeader);
            this.BuildListView.DrawItem += new System.Windows.Forms.DrawListViewItemEventHandler(this.BuildListView_DrawItem);
            this.BuildListView.DrawSubItem += new System.Windows.Forms.DrawListViewSubItemEventHandler(this.BuildListView_DrawSubItem);
            this.BuildListView.SelectedIndexChanged += new System.EventHandler(this.BuildListView_SelectedIndexChanged);
            this.BuildListView.FontChanged += new System.EventHandler(this.BuildListView_FontChanged);
            this.BuildListView.MouseClick += new System.Windows.Forms.MouseEventHandler(this.BuildListView_MouseClick);
            this.BuildListView.MouseUp += new System.Windows.Forms.MouseEventHandler(this.BuildListView_MouseUp);
            // 
            // IconHeader
            // 
            this.IconHeader.Text = "";
            this.IconHeader.Width = 34;
            // 
            // ChangeHeader
            // 
            this.ChangeHeader.Text = "Change";
            this.ChangeHeader.TextAlign = System.Windows.Forms.HorizontalAlignment.Center;
            this.ChangeHeader.Width = 75;
            // 
            // TypeHeader
            // 
            this.TypeHeader.Text = "Type";
            this.TypeHeader.TextAlign = System.Windows.Forms.HorizontalAlignment.Center;
            this.TypeHeader.Width = 99;
            // 
            // AuthorHeader
            // 
            this.AuthorHeader.Text = "Author";
            this.AuthorHeader.TextAlign = System.Windows.Forms.HorizontalAlignment.Center;
            this.AuthorHeader.Width = 121;
            // 
            // DescriptionHeader
            // 
            this.DescriptionHeader.Text = "Description";
            this.DescriptionHeader.Width = 777;
            // 
            // AssignToMeBtn
            // 
            this.AssignToMeBtn.Anchor = System.Windows.Forms.AnchorStyles.None;
            this.AssignToMeBtn.Location = new System.Drawing.Point(138, 3);
            this.AssignToMeBtn.Name = "AssignToMeBtn";
            this.AssignToMeBtn.Size = new System.Drawing.Size(129, 27);
            this.AssignToMeBtn.TabIndex = 1;
            this.AssignToMeBtn.Text = "Assign To Me";
            this.AssignToMeBtn.UseVisualStyleBackColor = true;
            this.AssignToMeBtn.Click += new System.EventHandler(this.AssignToMeBtn_Click);
            // 
            // MarkFixedBtn
            // 
            this.MarkFixedBtn.Anchor = System.Windows.Forms.AnchorStyles.None;
            this.MarkFixedBtn.Location = new System.Drawing.Point(952, 3);
            this.MarkFixedBtn.Name = "MarkFixedBtn";
            this.MarkFixedBtn.Size = new System.Drawing.Size(107, 27);
            this.MarkFixedBtn.TabIndex = 3;
            this.MarkFixedBtn.Text = "Mark Fixed...";
            this.MarkFixedBtn.UseVisualStyleBackColor = true;
            this.MarkFixedBtn.Click += new System.EventHandler(this.MarkFixedBtn_Click);
            // 
            // ReopenBtn
            // 
            this.ReopenBtn.Anchor = System.Windows.Forms.AnchorStyles.None;
            this.ReopenBtn.Location = new System.Drawing.Point(838, 3);
            this.ReopenBtn.Name = "ReopenBtn";
            this.ReopenBtn.Size = new System.Drawing.Size(108, 27);
            this.ReopenBtn.TabIndex = 5;
            this.ReopenBtn.Text = "Reopen";
            this.ReopenBtn.UseVisualStyleBackColor = true;
            this.ReopenBtn.Click += new System.EventHandler(this.ReopenBtn_Click);
            // 
            // tableLayoutPanel4
            // 
            this.tableLayoutPanel4.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.tableLayoutPanel4.ColumnCount = 1;
            this.tableLayoutPanel4.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 100F));
            this.tableLayoutPanel4.Controls.Add(this.splitContainer1, 0, 1);
            this.tableLayoutPanel4.Controls.Add(this.tableLayoutPanel5, 0, 2);
            this.tableLayoutPanel4.Controls.Add(this.tableLayoutPanel1, 0, 0);
            this.tableLayoutPanel4.Location = new System.Drawing.Point(12, 12);
            this.tableLayoutPanel4.Name = "tableLayoutPanel4";
            this.tableLayoutPanel4.RowCount = 3;
            this.tableLayoutPanel4.RowStyles.Add(new System.Windows.Forms.RowStyle());
            this.tableLayoutPanel4.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Percent, 100F));
            this.tableLayoutPanel4.RowStyles.Add(new System.Windows.Forms.RowStyle());
            this.tableLayoutPanel4.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Absolute, 20F));
            this.tableLayoutPanel4.Size = new System.Drawing.Size(1171, 696);
            this.tableLayoutPanel4.TabIndex = 6;
            // 
            // splitContainer1
            // 
            this.splitContainer1.Dock = System.Windows.Forms.DockStyle.Fill;
            this.splitContainer1.Location = new System.Drawing.Point(3, 127);
            this.splitContainer1.Name = "splitContainer1";
            this.splitContainer1.Orientation = System.Windows.Forms.Orientation.Horizontal;
            // 
            // splitContainer1.Panel1
            // 
            this.splitContainer1.Panel1.Controls.Add(this.panel1);
            // 
            // splitContainer1.Panel2
            // 
            this.splitContainer1.Panel2.Controls.Add(this.groupBox1);
            this.splitContainer1.Size = new System.Drawing.Size(1165, 519);
            this.splitContainer1.SplitterDistance = 121;
            this.splitContainer1.SplitterWidth = 12;
            this.splitContainer1.TabIndex = 1;
            // 
            // panel1
            // 
            this.panel1.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.panel1.BackColor = System.Drawing.SystemColors.Window;
            this.panel1.BorderStyle = System.Windows.Forms.BorderStyle.FixedSingle;
            this.panel1.Controls.Add(this.DetailsTextBox);
            this.panel1.Location = new System.Drawing.Point(6, 3);
            this.panel1.Name = "panel1";
            this.panel1.Size = new System.Drawing.Size(1156, 117);
            this.panel1.TabIndex = 1;
            // 
            // DetailsTextBox
            // 
            this.DetailsTextBox.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.DetailsTextBox.BackColor = System.Drawing.SystemColors.Window;
            this.DetailsTextBox.BorderStyle = System.Windows.Forms.BorderStyle.None;
            this.DetailsTextBox.Font = new System.Drawing.Font("Courier New", 8.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(204)));
            this.DetailsTextBox.Location = new System.Drawing.Point(8, 8);
            this.DetailsTextBox.Margin = new System.Windows.Forms.Padding(0);
            this.DetailsTextBox.Multiline = true;
            this.DetailsTextBox.Name = "DetailsTextBox";
            this.DetailsTextBox.ReadOnly = true;
            this.DetailsTextBox.ScrollBars = System.Windows.Forms.ScrollBars.Both;
            this.DetailsTextBox.Size = new System.Drawing.Size(1138, 98);
            this.DetailsTextBox.TabIndex = 0;
            this.DetailsTextBox.Text = "This is an error";
            this.DetailsTextBox.WordWrap = false;
            // 
            // tableLayoutPanel5
            // 
            this.tableLayoutPanel5.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Left | System.Windows.Forms.AnchorStyles.Right)));
            this.tableLayoutPanel5.AutoSize = true;
            this.tableLayoutPanel5.AutoSizeMode = System.Windows.Forms.AutoSizeMode.GrowAndShrink;
            this.tableLayoutPanel5.ColumnCount = 7;
            this.tableLayoutPanel5.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle());
            this.tableLayoutPanel5.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle());
            this.tableLayoutPanel5.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle());
            this.tableLayoutPanel5.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 100F));
            this.tableLayoutPanel5.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle());
            this.tableLayoutPanel5.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle());
            this.tableLayoutPanel5.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle());
            this.tableLayoutPanel5.Controls.Add(this.AssignBtn, 0, 0);
            this.tableLayoutPanel5.Controls.Add(this.AssignToMeBtn, 1, 0);
            this.tableLayoutPanel5.Controls.Add(this.MarkFixedBtn, 5, 0);
            this.tableLayoutPanel5.Controls.Add(this.OkBtn, 6, 0);
            this.tableLayoutPanel5.Controls.Add(this.ReopenBtn, 4, 0);
            this.tableLayoutPanel5.Controls.Add(this.AssignToOtherBtn, 2, 0);
            this.tableLayoutPanel5.Location = new System.Drawing.Point(3, 660);
            this.tableLayoutPanel5.Margin = new System.Windows.Forms.Padding(3, 11, 3, 3);
            this.tableLayoutPanel5.Name = "tableLayoutPanel5";
            this.tableLayoutPanel5.RowCount = 1;
            this.tableLayoutPanel5.RowStyles.Add(new System.Windows.Forms.RowStyle());
            this.tableLayoutPanel5.Size = new System.Drawing.Size(1165, 33);
            this.tableLayoutPanel5.TabIndex = 1;
            // 
            // JobContextMenu
            // 
            this.JobContextMenu.Items.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.JobContextMenu_ViewJob,
            this.JobContextMenu_StepSeparatorMin,
            this.toolStripMenuItem1,
            this.JobContextMenu_StepSeparatorMax,
            this.JobContextMenu_ShowFirstError});
            this.JobContextMenu.Name = "JobContextMenu";
            this.JobContextMenu.Size = new System.Drawing.Size(151, 82);
            // 
            // JobContextMenu_ShowFirstError
            // 
            this.JobContextMenu_ShowFirstError.Font = new System.Drawing.Font("Segoe UI", 9F);
            this.JobContextMenu_ShowFirstError.Name = "JobContextMenu_ShowFirstError";
            this.JobContextMenu_ShowFirstError.Size = new System.Drawing.Size(150, 22);
            this.JobContextMenu_ShowFirstError.Text = "View first error";
            this.JobContextMenu_ShowFirstError.Click += new System.EventHandler(this.JobContextMenu_ShowError_Click);
            // 
            // JobContextMenu_StepSeparatorMin
            // 
            this.JobContextMenu_StepSeparatorMin.Name = "JobContextMenu_StepSeparatorMin";
            this.JobContextMenu_StepSeparatorMin.Size = new System.Drawing.Size(147, 6);
            // 
            // toolStripMenuItem1
            // 
            this.toolStripMenuItem1.Name = "toolStripMenuItem1";
            this.toolStripMenuItem1.Size = new System.Drawing.Size(150, 22);
            this.toolStripMenuItem1.Text = "Step: XYZ";
            // 
            // JobContextMenu_StepSeparatorMax
            // 
            this.JobContextMenu_StepSeparatorMax.Name = "JobContextMenu_StepSeparatorMax";
            this.JobContextMenu_StepSeparatorMax.Size = new System.Drawing.Size(147, 6);
            // 
            // JobContextMenu_ViewJob
            // 
            this.JobContextMenu_ViewJob.Name = "JobContextMenu_ViewJob";
            this.JobContextMenu_ViewJob.Size = new System.Drawing.Size(150, 22);
            this.JobContextMenu_ViewJob.Text = "View Job...";
            this.JobContextMenu_ViewJob.Click += new System.EventHandler(this.JobContextMenu_ViewJob_Click);
            // 
            // IssueDetailsWindow
            // 
            this.AcceptButton = this.OkBtn;
            this.AutoScaleDimensions = new System.Drawing.SizeF(96F, 96F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Dpi;
            this.CancelButton = this.OkBtn;
            this.ClientSize = new System.Drawing.Size(1195, 720);
            this.Controls.Add(this.tableLayoutPanel4);
            this.Font = new System.Drawing.Font("Segoe UI", 9F);
            this.Name = "IssueDetailsWindow";
            this.ShowIcon = false;
            this.ShowInTaskbar = false;
            this.StartPosition = System.Windows.Forms.FormStartPosition.CenterParent;
            this.Text = "Issue Details";
            this.BuildListContextMenu.ResumeLayout(false);
            this.tableLayoutPanel1.ResumeLayout(false);
            this.tableLayoutPanel1.PerformLayout();
            this.groupBox1.ResumeLayout(false);
            this.tableLayoutPanel2.ResumeLayout(false);
            this.tableLayoutPanel2.PerformLayout();
            this.tableLayoutPanel3.ResumeLayout(false);
            this.tableLayoutPanel3.PerformLayout();
            this.tableLayoutPanel4.ResumeLayout(false);
            this.tableLayoutPanel4.PerformLayout();
            this.splitContainer1.Panel1.ResumeLayout(false);
            this.splitContainer1.Panel2.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)(this.splitContainer1)).EndInit();
            this.splitContainer1.ResumeLayout(false);
            this.panel1.ResumeLayout(false);
            this.panel1.PerformLayout();
            this.tableLayoutPanel5.ResumeLayout(false);
            this.JobContextMenu.ResumeLayout(false);
            this.ResumeLayout(false);

		}

		#endregion

		private CustomListViewControl BuildListView;
		private System.Windows.Forms.ColumnHeader ChangeHeader;
		private System.Windows.Forms.ColumnHeader TypeHeader;
		private System.Windows.Forms.ColumnHeader AuthorHeader;
		private System.Windows.Forms.ColumnHeader DescriptionHeader;
		private System.Windows.Forms.ComboBox StreamComboBox;
		private System.Windows.Forms.Button OkBtn;
		private System.Windows.Forms.Button AssignToOtherBtn;
		private System.Windows.Forms.ColumnHeader IconHeader;
		private System.Windows.Forms.ContextMenuStrip BuildListContextMenu;
		private System.Windows.Forms.ToolStripMenuItem BuildListContextMenu_MoreInfo;
		private System.Windows.Forms.ToolStripMenuItem BuildListContextMenu_Assign;
		private System.Windows.Forms.ToolStripSeparator toolStripSeparator1;
		private System.Windows.Forms.TextBox OpenSinceTextBox;
		private System.Windows.Forms.Button AssignBtn;
		private System.Windows.Forms.Label label1;
		private System.Windows.Forms.Label label3;
		private System.Windows.Forms.Label label5;
		private System.Windows.Forms.TextBox StatusTextBox;
		private System.Windows.Forms.TableLayoutPanel tableLayoutPanel1;
		private System.Windows.Forms.GroupBox groupBox1;
		private TextBoxWithCueBanner FilterTextBox;
		private System.Windows.Forms.ComboBox FilterTypeComboBox;
		private System.Windows.Forms.TableLayoutPanel tableLayoutPanel2;
		private System.Windows.Forms.TableLayoutPanel tableLayoutPanel3;
		private System.Windows.Forms.Button AssignToMeBtn;
		private System.Windows.Forms.Button MarkFixedBtn;
		private System.Windows.Forms.Button ReopenBtn;
		private System.Windows.Forms.TableLayoutPanel tableLayoutPanel4;
		private System.Windows.Forms.TableLayoutPanel tableLayoutPanel5;
		private System.Windows.Forms.TextBox DetailsTextBox;
		private System.Windows.Forms.SplitContainer splitContainer1;
		private System.Windows.Forms.Panel panel1;
		private System.Windows.Forms.TextBox StepNamesTextBox;
		private System.Windows.Forms.Label label2;
		private System.Windows.Forms.Label label4;
		private System.Windows.Forms.TextBox SummaryTextBox;
		private System.Windows.Forms.TextBox StreamNamesTextBox;
		private System.Windows.Forms.ContextMenuStrip JobContextMenu;
		private System.Windows.Forms.ToolStripMenuItem JobContextMenu_ShowFirstError;
		private System.Windows.Forms.ToolStripSeparator JobContextMenu_StepSeparatorMin;
		private System.Windows.Forms.ToolStripMenuItem toolStripMenuItem1;
		private System.Windows.Forms.ToolStripSeparator JobContextMenu_StepSeparatorMax;
		private System.Windows.Forms.ToolStripMenuItem JobContextMenu_ViewJob;
	}
}