import React from 'react';


type Props = {

  onChangeIcon: () => void;
  onRenameTabModal: () => void;
  onDuplicateTab: () => void;
  onAddSnapshotTab: () => void;
  onAddSequencerTab: () => void;
}

export class Tab extends React.Component<Props> {
  render() {
    return (
      <div className="tabs-tab">
        <div className="button-list">
          <button tabIndex={-1} className="btn btn-secondary" onClick={this.props.onChangeIcon}>Change tab icon</button>
          <button tabIndex={-1} className="btn btn-secondary" onClick={this.props.onRenameTabModal}>Rename Tab</button>
          <button tabIndex={-1} className="btn btn-secondary" onClick={this.props.onDuplicateTab}>Duplicate Tab</button>
          <button tabIndex={-1} className="btn btn-secondary" onClick={this.props.onAddSnapshotTab}>Add Snapshot Tab</button>
          <button tabIndex={-1} className="btn btn-secondary" onClick={this.props.onAddSequencerTab}>Add Sequencer Tab</button>
        </div>
      </div>
    );
  }
};
