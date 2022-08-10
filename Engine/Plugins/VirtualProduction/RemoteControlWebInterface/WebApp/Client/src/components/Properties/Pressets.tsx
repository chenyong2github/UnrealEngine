import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import React from 'react';
import { IPreset } from 'src/shared';
import { Search } from '../controls';


type Props = {
  preset: IPreset;
  pressets: IPreset[];

  onPresetChange: (preset: IPreset) => void;
  onSearch: (value: string) => void;
}

export class Pressets extends React.Component<Props> {

  render() {
    const { preset, pressets } = this.props;

    return (
      <div className="presets-tab">
        <Search placeholder="Search Presets" onSearch={this.props.onSearch} />
        <div className="presets-wrapper">
          {pressets.map(p => {
            let className = 'btn preset-btn ';

            if (p.ID === preset?.ID)
              className += 'active';

            return (
              <div key={p?.ID} className={className} onClick={this.props.onPresetChange.bind(this, p)}>
                {p?.Name}
                <div className="item-icon">
                  <FontAwesomeIcon icon={['fas', 'external-link-square-alt']} />
                </div>
              </div>
            );
          })}
        </div>
      </div>
    );
  }
};
