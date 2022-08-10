import React from 'react';


type SliderWheelProps = {
  onWheelMove?: (value: number, offset?: number) => void;
  onWheelStart?: () => void;
}

type SliderWheelState = {
  offset: number;
}

export class SliderWheel extends React.Component<SliderWheelProps, SliderWheelState> {

  state: SliderWheelState = {
    offset: 0,
  };

  ref = React.createRef<HTMLDivElement>();
  monitoring: boolean = false;
  last: number = -1;
  sum: number = 0;

  onPointerDown = (e: React.PointerEvent<HTMLDivElement>) => {
    if (!this.ref.current)
      return;


    this.monitoring = true;
    this.last = e.clientX;
    this.ref.current.setPointerCapture(e.pointerId);
    this.props.onWheelStart?.();
  }

  onPointerMove = (e: React.PointerEvent<HTMLDivElement>) => {
    if (!this.monitoring)
      return;

    const delta = e.clientX - this.last;
    if (Math.abs(delta) < 2)
      return; 

    this.last = e.clientX;
    let { offset } = this.state;

    this.sum += delta;
    const rect = this.ref.current.getBoundingClientRect();

    offset += delta;
    offset %= rect.width / 2;

    this.props.onWheelMove?.(Math.sign(delta), this.sum / (rect.width / 2));
    this.setState({ offset });
  }

  onPointerUp = (e: React.PointerEvent<HTMLDivElement>) => {
    if (!this.monitoring)
      return;

    this.monitoring = false;
    this.last = 0;
    this.sum = 0;
    this.ref.current.releasePointerCapture(e.pointerId);
  }

  renderCircles = () => {
    const circles = [];

    for (let i = 0; i < 80; i++)
      circles.push(<div key={i} className="slider-circle" />);

    return circles;
  }

  render() {
    const { offset } = this.state;
    const style: React.CSSProperties = { transform: `translateX(${offset}px)` };

    return (
      <div className="color-picker-slider-wheel"
           onPointerMove={this.onPointerMove}
           onPointerDown={this.onPointerDown}
           onPointerUp={this.onPointerUp}
           ref={this.ref}>
        <div className="circles-list" style={style}>
          {this.renderCircles()}
        </div>
      </div>
    );
  }
}


