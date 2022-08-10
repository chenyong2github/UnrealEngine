import React from 'react';
import crypto from 'crypto';
import { ReactComponent as Logo } from '../assets/ue_logo.svg';
import { _api } from '../reducers';


type Props = {

}

type State = {
  passphrase: string;
  error: boolean;
}

export class Login extends React.Component<Props, State> {
  state: State = {
    passphrase: '',
    error: false,
  }

  onChange = (passphrase: string) => {
    this.setState({ passphrase });
  }

  onKeyPress = async (event: React.KeyboardEvent<HTMLInputElement>) => {
    if (event.key !== "Enter")
      return;

    const { passphrase } = this.state;
    const secured = crypto.createHash('md5')
                          .update(passphrase).digest('hex');

    const error = await _api.passphrase.login(secured);
    this.setState({ error });
  }

  render() {
    return (
      <div className="fullscreen login-screen">
        <div className='icon-wrapper'>
          <div className="app-icon">
            <Logo className="logo" />
          </div>
          <div>Remote Control Web App</div>
        </div>
        <div className='form'>
          Password
          <input onChange={e => this.onChange(e.target.value)}
                  onKeyPress={this.onKeyPress}
                  type='password' />

          {this.state.error &&
            <label className='login-status'>Incorrect Password</label>
          }
        </div>
     </div>
    );
  }
};