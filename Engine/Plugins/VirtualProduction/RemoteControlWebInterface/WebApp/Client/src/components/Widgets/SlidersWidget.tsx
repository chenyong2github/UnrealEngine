import React from 'react';
import { ICustomStackProperty } from 'src/shared';
import { WidgetUtilities } from 'src/utilities';
import { ValueInput, Slider } from '../controls';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';


type Props = {
  widget: ICustomStackProperty;
  label?: React.ReactNode;
  min?: number;
  max?: number;
  value?: any;
  
  onChange?: (widget: ICustomStackProperty, axis?: string, axisValue?: number, locked?: boolean) => any;
  onPrecisionModal?: (property: string) => void;
}

export class SlidersWidget extends React.Component<Props> {

  render() {   
    const { widget, label = '', min, max, value } = this.props;

    const propertyType = widget?.propertyType;
    const properties = WidgetUtilities.getPropertyKeys(propertyType);
    const precision = WidgetUtilities.getPropertyPrecision(propertyType);

    const isSlider = (min !== undefined && max !== undefined);
    const selectedProperties = properties.filter(property => widget.widgets?.includes(property));

    return (
      <div className="custom-sliders">
        {properties.map(property =>
          <React.Fragment key={property}>
            {(widget.widgets?.includes(property) || !selectedProperties.length) && 
              <div className="slider-row">
                <div className="title">{label}.{property}</div>
                <FontAwesomeIcon icon={['fas', 'expand']} className="expand-icon" onClick={() => this.props.onPrecisionModal?.(property)} />
                <ValueInput min={min}
                            max={max}
                            precision={precision}
                            value={value?.[property]}
                            onChange={value => this.props.onChange?.(widget, property, value) || null} />
                {isSlider &&
                  <>
                    <div className="limits">{min?.toFixed(1)}</div>
                    <Slider value={value?.[property] || null}
                            min={min}
                            max={max}
                            showLabel={false}
                            onChange={value => this.props.onChange?.(widget, property, value) || null} />
                    <div className="limits">{max?.toFixed(1)}</div>
                  </>
                }
              </div>
            }
          </React.Fragment>
        )}
        <FontAwesomeIcon icon={['fas', 'undo']} className="reset-sliders" onClick={() => this.props.onChange?.(widget)} />
      </div>
    );
  }
};