import { Route, Router, Switch } from 'wouter';
import { useHashLocation } from 'wouter/use-hash-location';

import { Shell } from './components/Shell';
import { Activity } from './pages/Activity';
import { Alerts } from './pages/Alerts';
import { Login, Offline, Setup } from './pages/Auth';
import { Climate } from './pages/Climate';
import { Overview } from './pages/Overview';
import { PlantDetail } from './pages/PlantDetail';
import { PlantEditor } from './pages/PlantEditor';
import { Settings } from './pages/Settings';
import { DeviceProvider, useDevice } from './state/device';
import { ToastProvider } from './state/toast';

function Routes() {
  const { phase } = useDevice();
  if (phase === 'loading') return <div className="auth muted">Connecting…</div>;
  if (phase === 'offline') return <Offline />;
  if (phase === 'setup') return <Setup />;
  if (phase === 'login') return <Login />;
  return (
    <Shell>
      <Switch>
        <Route path="/" component={Overview} />
        <Route path="/plants/new">{() => <PlantEditor />}</Route>
        <Route path="/plants/new/:node">{(p) => <PlantEditor node={decodeURIComponent(p.node)} />}</Route>
        <Route path="/plants/:id/edit">{(p) => <PlantEditor id={decodeURIComponent(p.id)} />}</Route>
        <Route path="/plants/:id">{(p) => <PlantDetail id={decodeURIComponent(p.id)} />}</Route>
        <Route path="/activity" component={Activity} />
        <Route path="/alerts" component={Alerts} />
        <Route path="/climate" component={Climate} />
        <Route path="/settings" component={Settings} />
        <Route>
          <div className="page">
            <h1>Page not found</h1>
            <a href="#/">Back to your plants</a>
          </div>
        </Route>
      </Switch>
    </Shell>
  );
}

export function App() {
  return (
    <Router hook={useHashLocation}>
      <ToastProvider>
        <DeviceProvider>
          <Routes />
        </DeviceProvider>
      </ToastProvider>
    </Router>
  );
}
