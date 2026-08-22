// AEROLAB RESILIENCE - interface error boundary.
//
// Added after a real failure: the Restart button was wired as
// `onClick={props.onRestart}`, so React passed the MouseEvent as the seed
// argument, `core.reset(MouseEvent)` aborted the WebAssembly module, and the
// whole page went black with no way back except a full reload.
//
// The wiring bug is fixed. The boundary stays because a page whose only
// recovery path is "reload and lose your run" is not a tool you can demonstrate
// with. It resets the view, keeps the message visible, and says plainly that
// the simulation core itself is not the thing that failed.
import { Component } from "react";
import type { ErrorInfo, ReactNode } from "react";

interface Props {
  children: ReactNode;
  message: string;
  resetLabel: string;
  onReset: () => void;
}

interface State {
  error: Error | null;
}

export class ErrorBoundary extends Component<Props, State> {
  state: State = { error: null };

  static getDerivedStateFromError(error: Error): State {
    return { error };
  }

  componentDidCatch(error: Error, info: ErrorInfo) {
    // Kept on the console on purpose: the end-to-end test asserts there are no
    // console errors on the nominal path, so anything printed here is a real
    // regression rather than noise.
    console.error("AEROLAB interface error", error, info.componentStack);
  }

  render() {
    if (this.state.error) {
      return (
        <div className="pad">
          <div className="banner-error">
            <p style={{ marginTop: 0 }}>{this.props.message}</p>
            <pre style={{ margin: "10px 0", whiteSpace: "pre-wrap" }}>
              {this.state.error.message}
            </pre>
            <button
              type="button"
              onClick={() => {
                this.setState({ error: null });
                this.props.onReset();
              }}
            >
              {this.props.resetLabel}
            </button>
          </div>
        </div>
      );
    }
    return this.props.children;
  }
}
