namespace CruncherSharp
{
    partial class CruncherSharp_Form
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
            System.Windows.Forms.DataGridViewCellStyle dataGridViewCellStyle12 = new System.Windows.Forms.DataGridViewCellStyle();
            System.Windows.Forms.DataGridViewCellStyle dataGridViewCellStyle13 = new System.Windows.Forms.DataGridViewCellStyle();
            System.Windows.Forms.DataGridViewCellStyle dataGridViewCellStyle14 = new System.Windows.Forms.DataGridViewCellStyle();
            System.Windows.Forms.DataGridViewCellStyle dataGridViewCellStyle19 = new System.Windows.Forms.DataGridViewCellStyle();
            System.Windows.Forms.DataGridViewCellStyle dataGridViewCellStyle20 = new System.Windows.Forms.DataGridViewCellStyle();
            System.Windows.Forms.DataGridViewCellStyle dataGridViewCellStyle21 = new System.Windows.Forms.DataGridViewCellStyle();
            System.Windows.Forms.DataGridViewCellStyle dataGridViewCellStyle22 = new System.Windows.Forms.DataGridViewCellStyle();
            this.mainMenu = new System.Windows.Forms.MenuStrip();
            this.fileToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
            this.loadPDBToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
            this.exitToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
            this.openPdbDialog = new System.Windows.Forms.OpenFileDialog();
            this.bindingSourceSymbols = new System.Windows.Forms.BindingSource(this.components);
            this.splitContainer2 = new System.Windows.Forms.SplitContainer();
            this.buttonHistoryForward = new System.Windows.Forms.Button();
            this.buttonHistoryBack = new System.Windows.Forms.Button();
            this.filterFeedbackLabel = new System.Windows.Forms.Label();
            this.textBoxCache = new System.Windows.Forms.MaskedTextBox();
            this.label2 = new System.Windows.Forms.Label();
            this.label1 = new System.Windows.Forms.Label();
            this.textBoxFilter = new System.Windows.Forms.TextBox();
            this.splitContainer1 = new System.Windows.Forms.SplitContainer();
            this.loadingProgressBar = new System.Windows.Forms.ProgressBar();
            this.dataGridSymbols = new System.Windows.Forms.DataGridView();
            this.dataGridViewSymbolInfo = new System.Windows.Forms.DataGridView();
            this.colField = new System.Windows.Forms.DataGridViewTextBoxColumn();
            this.colFieldOffset = new System.Windows.Forms.DataGridViewTextBoxColumn();
            this.colFieldSize = new System.Windows.Forms.DataGridViewTextBoxColumn();
            this.contextMenuStrip1 = new System.Windows.Forms.ContextMenuStrip(this.components);
            this.copyTypeLayoutToClipboardToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
            this.setPrefetchStartOffsetToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
            this.loadingBackgroundWorker = new System.ComponentModel.BackgroundWorker();
            this.reloadCurrentToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
            this.mainMenu.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)(this.bindingSourceSymbols)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.splitContainer2)).BeginInit();
            this.splitContainer2.Panel1.SuspendLayout();
            this.splitContainer2.Panel2.SuspendLayout();
            this.splitContainer2.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)(this.splitContainer1)).BeginInit();
            this.splitContainer1.Panel1.SuspendLayout();
            this.splitContainer1.Panel2.SuspendLayout();
            this.splitContainer1.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)(this.dataGridSymbols)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.dataGridViewSymbolInfo)).BeginInit();
            this.contextMenuStrip1.SuspendLayout();
            this.SuspendLayout();
            // 
            // mainMenu
            // 
            this.mainMenu.Items.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.fileToolStripMenuItem});
            this.mainMenu.Location = new System.Drawing.Point(0, 0);
            this.mainMenu.Name = "mainMenu";
            this.mainMenu.Size = new System.Drawing.Size(1304, 24);
            this.mainMenu.TabIndex = 0;
            this.mainMenu.Text = "menuStrip1";
            // 
            // fileToolStripMenuItem
            // 
            this.fileToolStripMenuItem.DropDownItems.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.loadPDBToolStripMenuItem,
            this.reloadCurrentToolStripMenuItem,
            this.exitToolStripMenuItem});
            this.fileToolStripMenuItem.Name = "fileToolStripMenuItem";
            this.fileToolStripMenuItem.Size = new System.Drawing.Size(37, 20);
            this.fileToolStripMenuItem.Text = "File";
            // 
            // loadPDBToolStripMenuItem
            // 
            this.loadPDBToolStripMenuItem.Name = "loadPDBToolStripMenuItem";
            this.loadPDBToolStripMenuItem.ShortcutKeys = ((System.Windows.Forms.Keys)((System.Windows.Forms.Keys.Control | System.Windows.Forms.Keys.O)));
            this.loadPDBToolStripMenuItem.Size = new System.Drawing.Size(177, 22);
            this.loadPDBToolStripMenuItem.Text = "Load PDB...";
            this.loadPDBToolStripMenuItem.Click += new System.EventHandler(this.loadPDBToolStripMenuItem_Click);
            // 
            // exitToolStripMenuItem
            // 
            this.exitToolStripMenuItem.Name = "exitToolStripMenuItem";
            this.exitToolStripMenuItem.Size = new System.Drawing.Size(177, 22);
            this.exitToolStripMenuItem.Text = "Exit";
            this.exitToolStripMenuItem.Click += new System.EventHandler(this.exitToolStripMenuItem_Click);
            // 
            // openPdbDialog
            // 
            this.openPdbDialog.Filter = "Symbol files|*.pdb|All files|*.*";
            // 
            // splitContainer2
            // 
            this.splitContainer2.Dock = System.Windows.Forms.DockStyle.Fill;
            this.splitContainer2.IsSplitterFixed = true;
            this.splitContainer2.Location = new System.Drawing.Point(0, 24);
            this.splitContainer2.Name = "splitContainer2";
            this.splitContainer2.Orientation = System.Windows.Forms.Orientation.Horizontal;
            // 
            // splitContainer2.Panel1
            // 
            this.splitContainer2.Panel1.Controls.Add(this.buttonHistoryForward);
            this.splitContainer2.Panel1.Controls.Add(this.buttonHistoryBack);
            this.splitContainer2.Panel1.Controls.Add(this.filterFeedbackLabel);
            this.splitContainer2.Panel1.Controls.Add(this.textBoxCache);
            this.splitContainer2.Panel1.Controls.Add(this.label2);
            this.splitContainer2.Panel1.Controls.Add(this.label1);
            this.splitContainer2.Panel1.Controls.Add(this.textBoxFilter);
            // 
            // splitContainer2.Panel2
            // 
            this.splitContainer2.Panel2.Controls.Add(this.splitContainer1);
            this.splitContainer2.Size = new System.Drawing.Size(1304, 812);
            this.splitContainer2.SplitterDistance = 59;
            this.splitContainer2.TabIndex = 1;
            // 
            // buttonHistoryForward
            // 
            this.buttonHistoryForward.Location = new System.Drawing.Point(272, -1);
            this.buttonHistoryForward.Name = "buttonHistoryForward";
            this.buttonHistoryForward.Size = new System.Drawing.Size(28, 23);
            this.buttonHistoryForward.TabIndex = 7;
            this.buttonHistoryForward.Text = "->";
            this.buttonHistoryForward.UseVisualStyleBackColor = true;
            this.buttonHistoryForward.Click += new System.EventHandler(this.buttonHistoryForward_Click);
            // 
            // buttonHistoryBack
            // 
            this.buttonHistoryBack.Location = new System.Drawing.Point(238, -1);
            this.buttonHistoryBack.Name = "buttonHistoryBack";
            this.buttonHistoryBack.Size = new System.Drawing.Size(28, 23);
            this.buttonHistoryBack.TabIndex = 6;
            this.buttonHistoryBack.Text = "<-";
            this.buttonHistoryBack.UseVisualStyleBackColor = true;
            this.buttonHistoryBack.Click += new System.EventHandler(this.buttonHistoryBack_Click);
            // 
            // filterFeedbackLabel
            // 
            this.filterFeedbackLabel.Location = new System.Drawing.Point(306, 6);
            this.filterFeedbackLabel.Name = "filterFeedbackLabel";
            this.filterFeedbackLabel.Size = new System.Drawing.Size(120, 13);
            this.filterFeedbackLabel.TabIndex = 5;
            // 
            // textBoxCache
            // 
            this.textBoxCache.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Right)));
            this.textBoxCache.Location = new System.Drawing.Point(1258, 3);
            this.textBoxCache.Mask = "0000";
            this.textBoxCache.Name = "textBoxCache";
            this.textBoxCache.Size = new System.Drawing.Size(34, 20);
            this.textBoxCache.TabIndex = 4;
            this.textBoxCache.Text = "64";
            this.textBoxCache.KeyPress += new System.Windows.Forms.KeyPressEventHandler(this.textBoxCache_KeyPress);
            this.textBoxCache.Leave += new System.EventHandler(this.textBoxCache_Leave);
            // 
            // label2
            // 
            this.label2.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Right)));
            this.label2.AutoSize = true;
            this.label2.Location = new System.Drawing.Point(1195, 6);
            this.label2.Name = "label2";
            this.label2.Size = new System.Drawing.Size(57, 13);
            this.label2.TabIndex = 3;
            this.label2.Text = "Cache line";
            // 
            // label1
            // 
            this.label1.AutoSize = true;
            this.label1.Location = new System.Drawing.Point(12, 5);
            this.label1.Name = "label1";
            this.label1.Size = new System.Drawing.Size(29, 13);
            this.label1.TabIndex = 1;
            this.label1.Text = "Filter";
            // 
            // textBoxFilter
            // 
            this.textBoxFilter.Location = new System.Drawing.Point(53, 2);
            this.textBoxFilter.Name = "textBoxFilter";
            this.textBoxFilter.Size = new System.Drawing.Size(179, 20);
            this.textBoxFilter.TabIndex = 0;
            this.textBoxFilter.TextChanged += new System.EventHandler(this.textBoxFilter_TextChanged);
            // 
            // splitContainer1
            // 
            this.splitContainer1.Dock = System.Windows.Forms.DockStyle.Fill;
            this.splitContainer1.Location = new System.Drawing.Point(0, 0);
            this.splitContainer1.Name = "splitContainer1";
            // 
            // splitContainer1.Panel1
            // 
            this.splitContainer1.Panel1.Controls.Add(this.loadingProgressBar);
            this.splitContainer1.Panel1.Controls.Add(this.dataGridSymbols);
            // 
            // splitContainer1.Panel2
            // 
            this.splitContainer1.Panel2.Controls.Add(this.dataGridViewSymbolInfo);
            this.splitContainer1.Size = new System.Drawing.Size(1304, 749);
            this.splitContainer1.SplitterDistance = 759;
            this.splitContainer1.TabIndex = 2;
            // 
            // loadingProgressBar
            // 
            this.loadingProgressBar.Dock = System.Windows.Forms.DockStyle.Bottom;
            this.loadingProgressBar.Location = new System.Drawing.Point(0, 734);
            this.loadingProgressBar.Name = "loadingProgressBar";
            this.loadingProgressBar.Size = new System.Drawing.Size(759, 15);
            this.loadingProgressBar.TabIndex = 3;
            // 
            // dataGridSymbols
            // 
            this.dataGridSymbols.AllowUserToAddRows = false;
            this.dataGridSymbols.AllowUserToDeleteRows = false;
            this.dataGridSymbols.AllowUserToResizeRows = false;
            dataGridViewCellStyle12.BackColor = System.Drawing.Color.FromArgb(((int)(((byte)(224)))), ((int)(((byte)(224)))), ((int)(((byte)(224)))));
            this.dataGridSymbols.AlternatingRowsDefaultCellStyle = dataGridViewCellStyle12;
            dataGridViewCellStyle13.Alignment = System.Windows.Forms.DataGridViewContentAlignment.MiddleLeft;
            dataGridViewCellStyle13.BackColor = System.Drawing.SystemColors.Control;
            dataGridViewCellStyle13.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            dataGridViewCellStyle13.ForeColor = System.Drawing.SystemColors.WindowText;
            dataGridViewCellStyle13.SelectionBackColor = System.Drawing.SystemColors.Highlight;
            dataGridViewCellStyle13.SelectionForeColor = System.Drawing.SystemColors.HighlightText;
            dataGridViewCellStyle13.WrapMode = System.Windows.Forms.DataGridViewTriState.True;
            this.dataGridSymbols.ColumnHeadersDefaultCellStyle = dataGridViewCellStyle13;
            this.dataGridSymbols.ColumnHeadersHeightSizeMode = System.Windows.Forms.DataGridViewColumnHeadersHeightSizeMode.AutoSize;
            dataGridViewCellStyle14.Alignment = System.Windows.Forms.DataGridViewContentAlignment.MiddleLeft;
            dataGridViewCellStyle14.BackColor = System.Drawing.SystemColors.Window;
            dataGridViewCellStyle14.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            dataGridViewCellStyle14.ForeColor = System.Drawing.SystemColors.ControlText;
            dataGridViewCellStyle14.SelectionBackColor = System.Drawing.SystemColors.Highlight;
            dataGridViewCellStyle14.SelectionForeColor = System.Drawing.SystemColors.HighlightText;
            dataGridViewCellStyle14.WrapMode = System.Windows.Forms.DataGridViewTriState.False;
            this.dataGridSymbols.DefaultCellStyle = dataGridViewCellStyle14;
            this.dataGridSymbols.Dock = System.Windows.Forms.DockStyle.Fill;
            this.dataGridSymbols.Location = new System.Drawing.Point(0, 0);
            this.dataGridSymbols.Margin = new System.Windows.Forms.Padding(3, 3, 3, 30);
            this.dataGridSymbols.MultiSelect = false;
            this.dataGridSymbols.Name = "dataGridSymbols";
            this.dataGridSymbols.ReadOnly = true;
            dataGridViewCellStyle19.Alignment = System.Windows.Forms.DataGridViewContentAlignment.MiddleLeft;
            dataGridViewCellStyle19.BackColor = System.Drawing.SystemColors.Control;
            dataGridViewCellStyle19.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            dataGridViewCellStyle19.ForeColor = System.Drawing.SystemColors.WindowText;
            dataGridViewCellStyle19.SelectionBackColor = System.Drawing.SystemColors.Highlight;
            dataGridViewCellStyle19.SelectionForeColor = System.Drawing.SystemColors.HighlightText;
            dataGridViewCellStyle19.WrapMode = System.Windows.Forms.DataGridViewTriState.True;
            this.dataGridSymbols.RowHeadersDefaultCellStyle = dataGridViewCellStyle19;
            this.dataGridSymbols.RowHeadersVisible = false;
            this.dataGridSymbols.SelectionMode = System.Windows.Forms.DataGridViewSelectionMode.FullRowSelect;
            this.dataGridSymbols.Size = new System.Drawing.Size(759, 749);
            this.dataGridSymbols.TabIndex = 2;
            this.dataGridSymbols.SelectionChanged += new System.EventHandler(this.dataGridSymbols_SelectionChanged);
            this.dataGridSymbols.SortCompare += new System.Windows.Forms.DataGridViewSortCompareEventHandler(this.dataGridSymbols_SortCompare);
            // 
            // dataGridViewSymbolInfo
            // 
            this.dataGridViewSymbolInfo.AllowUserToAddRows = false;
            this.dataGridViewSymbolInfo.AllowUserToDeleteRows = false;
            this.dataGridViewSymbolInfo.AllowUserToResizeRows = false;
            dataGridViewCellStyle20.Alignment = System.Windows.Forms.DataGridViewContentAlignment.MiddleLeft;
            dataGridViewCellStyle20.BackColor = System.Drawing.SystemColors.Control;
            dataGridViewCellStyle20.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            dataGridViewCellStyle20.ForeColor = System.Drawing.SystemColors.WindowText;
            dataGridViewCellStyle20.SelectionBackColor = System.Drawing.SystemColors.Highlight;
            dataGridViewCellStyle20.SelectionForeColor = System.Drawing.SystemColors.HighlightText;
            dataGridViewCellStyle20.WrapMode = System.Windows.Forms.DataGridViewTriState.True;
            this.dataGridViewSymbolInfo.ColumnHeadersDefaultCellStyle = dataGridViewCellStyle20;
            this.dataGridViewSymbolInfo.ColumnHeadersHeightSizeMode = System.Windows.Forms.DataGridViewColumnHeadersHeightSizeMode.AutoSize;
            this.dataGridViewSymbolInfo.Columns.AddRange(new System.Windows.Forms.DataGridViewColumn[] {
            this.colField,
            this.colFieldOffset,
            this.colFieldSize});
            this.dataGridViewSymbolInfo.ContextMenuStrip = this.contextMenuStrip1;
            dataGridViewCellStyle21.Alignment = System.Windows.Forms.DataGridViewContentAlignment.MiddleLeft;
            dataGridViewCellStyle21.BackColor = System.Drawing.SystemColors.Window;
            dataGridViewCellStyle21.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            dataGridViewCellStyle21.ForeColor = System.Drawing.SystemColors.ControlText;
            dataGridViewCellStyle21.SelectionBackColor = System.Drawing.SystemColors.Highlight;
            dataGridViewCellStyle21.SelectionForeColor = System.Drawing.SystemColors.HighlightText;
            dataGridViewCellStyle21.WrapMode = System.Windows.Forms.DataGridViewTriState.False;
            this.dataGridViewSymbolInfo.DefaultCellStyle = dataGridViewCellStyle21;
            this.dataGridViewSymbolInfo.Dock = System.Windows.Forms.DockStyle.Fill;
            this.dataGridViewSymbolInfo.Location = new System.Drawing.Point(0, 0);
            this.dataGridViewSymbolInfo.Name = "dataGridViewSymbolInfo";
            this.dataGridViewSymbolInfo.ReadOnly = true;
            dataGridViewCellStyle22.Alignment = System.Windows.Forms.DataGridViewContentAlignment.MiddleLeft;
            dataGridViewCellStyle22.BackColor = System.Drawing.SystemColors.Control;
            dataGridViewCellStyle22.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            dataGridViewCellStyle22.ForeColor = System.Drawing.SystemColors.WindowText;
            dataGridViewCellStyle22.SelectionBackColor = System.Drawing.SystemColors.Highlight;
            dataGridViewCellStyle22.SelectionForeColor = System.Drawing.SystemColors.HighlightText;
            dataGridViewCellStyle22.WrapMode = System.Windows.Forms.DataGridViewTriState.True;
            this.dataGridViewSymbolInfo.RowHeadersDefaultCellStyle = dataGridViewCellStyle22;
            this.dataGridViewSymbolInfo.RowHeadersVisible = false;
            this.dataGridViewSymbolInfo.SelectionMode = System.Windows.Forms.DataGridViewSelectionMode.FullRowSelect;
            this.dataGridViewSymbolInfo.Size = new System.Drawing.Size(541, 749);
            this.dataGridViewSymbolInfo.TabIndex = 0;
            this.dataGridViewSymbolInfo.CellMouseDoubleClick += new System.Windows.Forms.DataGridViewCellMouseEventHandler(this.dataGridViewSymbolInfo_CellMouseDoubleClick);
            this.dataGridViewSymbolInfo.CellPainting += new System.Windows.Forms.DataGridViewCellPaintingEventHandler(this.dataGridViewSymbolInfo_CellPainting);
            this.dataGridViewSymbolInfo.SortCompare += new System.Windows.Forms.DataGridViewSortCompareEventHandler(this.dataGridSymbols_SortCompare);
            // 
            // colField
            // 
            this.colField.HeaderText = "Field";
            this.colField.Name = "colField";
            this.colField.ReadOnly = true;
            this.colField.Width = 210;
            // 
            // colFieldOffset
            // 
            this.colFieldOffset.HeaderText = "Offset";
            this.colFieldOffset.Name = "colFieldOffset";
            this.colFieldOffset.ReadOnly = true;
            // 
            // colFieldSize
            // 
            this.colFieldSize.HeaderText = "Size";
            this.colFieldSize.Name = "colFieldSize";
            this.colFieldSize.ReadOnly = true;
            // 
            // contextMenuStrip1
            // 
            this.contextMenuStrip1.Items.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.copyTypeLayoutToClipboardToolStripMenuItem,
            this.setPrefetchStartOffsetToolStripMenuItem});
            this.contextMenuStrip1.Name = "contextMenuStrip1";
            this.contextMenuStrip1.Size = new System.Drawing.Size(239, 48);
            // 
            // copyTypeLayoutToClipboardToolStripMenuItem
            // 
            this.copyTypeLayoutToClipboardToolStripMenuItem.Name = "copyTypeLayoutToClipboardToolStripMenuItem";
            this.copyTypeLayoutToClipboardToolStripMenuItem.Size = new System.Drawing.Size(238, 22);
            this.copyTypeLayoutToClipboardToolStripMenuItem.Text = "Copy Type Layout To Clipboard";
            this.copyTypeLayoutToClipboardToolStripMenuItem.Click += new System.EventHandler(this.copyTypeLayoutToClipboardToolStripMenuItem_Click);
            // 
            // setPrefetchStartOffsetToolStripMenuItem
            // 
            this.setPrefetchStartOffsetToolStripMenuItem.Name = "setPrefetchStartOffsetToolStripMenuItem";
            this.setPrefetchStartOffsetToolStripMenuItem.Size = new System.Drawing.Size(238, 22);
            this.setPrefetchStartOffsetToolStripMenuItem.Text = "Set Prefetch Start Offset";
            this.setPrefetchStartOffsetToolStripMenuItem.Click += new System.EventHandler(this.setPrefetchStartOffsetToolStripMenuItem_Click);
            // 
            // loadingBackgroundWorker
            // 
            this.loadingBackgroundWorker.WorkerReportsProgress = true;
            this.loadingBackgroundWorker.WorkerSupportsCancellation = true;
            this.loadingBackgroundWorker.DoWork += new System.ComponentModel.DoWorkEventHandler(this.loadingBackgroundWorker_DoWork);
            this.loadingBackgroundWorker.ProgressChanged += new System.ComponentModel.ProgressChangedEventHandler(this.loadingBackgroundWorker_ProgressChanged);
            this.loadingBackgroundWorker.RunWorkerCompleted += new System.ComponentModel.RunWorkerCompletedEventHandler(this.loadingBackgroundWorker_RunWorkerCompleted);
            // 
            // reloadCurrentToolStripMenuItem
            // 
            this.reloadCurrentToolStripMenuItem.Name = "reloadCurrentToolStripMenuItem";
            this.reloadCurrentToolStripMenuItem.ShortcutKeys = System.Windows.Forms.Keys.F5;
            this.reloadCurrentToolStripMenuItem.Size = new System.Drawing.Size(180, 22);
            this.reloadCurrentToolStripMenuItem.Text = "Reload Current";
            this.reloadCurrentToolStripMenuItem.Click += new System.EventHandler(this.reloadCurrentToolStripMenuItem_Click);
            // 
            // CruncherSharp_Form
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(1304, 836);
            this.Controls.Add(this.splitContainer2);
            this.Controls.Add(this.mainMenu);
            this.MainMenuStrip = this.mainMenu;
            this.Name = "CruncherSharp_Form";
            this.Text = "Cruncher #";
            this.mainMenu.ResumeLayout(false);
            this.mainMenu.PerformLayout();
            ((System.ComponentModel.ISupportInitialize)(this.bindingSourceSymbols)).EndInit();
            this.splitContainer2.Panel1.ResumeLayout(false);
            this.splitContainer2.Panel1.PerformLayout();
            this.splitContainer2.Panel2.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)(this.splitContainer2)).EndInit();
            this.splitContainer2.ResumeLayout(false);
            this.splitContainer1.Panel1.ResumeLayout(false);
            this.splitContainer1.Panel2.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)(this.splitContainer1)).EndInit();
            this.splitContainer1.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)(this.dataGridSymbols)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.dataGridViewSymbolInfo)).EndInit();
            this.contextMenuStrip1.ResumeLayout(false);
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        #endregion

        private System.Windows.Forms.MenuStrip mainMenu;
        private System.Windows.Forms.ToolStripMenuItem fileToolStripMenuItem;
        private System.Windows.Forms.ToolStripMenuItem loadPDBToolStripMenuItem;
        private System.Windows.Forms.ToolStripMenuItem exitToolStripMenuItem;
        private System.Windows.Forms.OpenFileDialog openPdbDialog;
        private System.Windows.Forms.BindingSource bindingSourceSymbols;
        private System.Windows.Forms.SplitContainer splitContainer2;
        private System.Windows.Forms.SplitContainer splitContainer1;
        private System.Windows.Forms.DataGridView dataGridSymbols;
        private System.Windows.Forms.DataGridView dataGridViewSymbolInfo;
        private System.Windows.Forms.Label label1;
        private System.Windows.Forms.TextBox textBoxFilter;
        private System.Windows.Forms.ContextMenuStrip contextMenuStrip1;
        private System.Windows.Forms.ToolStripMenuItem copyTypeLayoutToClipboardToolStripMenuItem;
        private System.Windows.Forms.Label label2;
        private System.Windows.Forms.MaskedTextBox textBoxCache;
        private System.Windows.Forms.ToolStripMenuItem setPrefetchStartOffsetToolStripMenuItem;
		private System.Windows.Forms.Label filterFeedbackLabel;
		private System.Windows.Forms.Button buttonHistoryForward;
		private System.Windows.Forms.Button buttonHistoryBack;
		private System.Windows.Forms.ProgressBar loadingProgressBar;
		private System.Windows.Forms.DataGridViewTextBoxColumn colField;
		private System.Windows.Forms.DataGridViewTextBoxColumn colFieldOffset;
		private System.Windows.Forms.DataGridViewTextBoxColumn colFieldSize;
		private System.ComponentModel.BackgroundWorker loadingBackgroundWorker;
		private System.Windows.Forms.ToolStripMenuItem reloadCurrentToolStripMenuItem;
	}
}

