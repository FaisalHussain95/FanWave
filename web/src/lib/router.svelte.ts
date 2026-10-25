// Hash routing: the hub serves one index.html, so every screen lives after the #.

export type Route =
  | { name: 'dashboard' }
  | { name: 'fan'; id: string }
  | { name: 'remote'; id: string }
  | { name: 'rooms' }
  | { name: 'settings' }
  | { name: 'lab' }
  | { name: 'detective'; captureId: number }
  | { name: 'pair' }
  | { name: 'learn' }
  | { name: 'setup' };

function parse(hash: string): Route {
  const parts = hash.replace(/^#\/?/, '').split('/').filter(Boolean).map(decodeURIComponent);
  switch (parts[0]) {
    case 'fan':
      if (!parts[1]) return { name: 'dashboard' };
      return parts[2] === 'remote' ? { name: 'remote', id: parts[1] } : { name: 'fan', id: parts[1] };
    case 'rooms':
      return { name: 'rooms' };
    case 'settings':
      return { name: 'settings' };
    case 'lab':
      if (parts[1] === 'capture' && Number.isFinite(Number(parts[2]))) {
        return { name: 'detective', captureId: Number(parts[2]) };
      }
      return { name: 'lab' };
    case 'pair':
      return parts[1] === 'learn' ? { name: 'learn' } : { name: 'pair' };
    case 'setup':
      return { name: 'setup' };
    default:
      return { name: 'dashboard' };
  }
}

export const router = $state({ route: parse(location.hash) });

window.addEventListener('hashchange', () => {
  router.route = parse(location.hash);
  window.scrollTo(0, 0);
});

export const href = {
  dashboard: '#/',
  fan: (id: string) => `#/fan/${encodeURIComponent(id)}`,
  remote: (id: string) => `#/fan/${encodeURIComponent(id)}/remote`,
  rooms: '#/rooms',
  settings: '#/settings',
  lab: '#/lab',
  detective: (captureId: number) => `#/lab/capture/${captureId}`,
  pair: '#/pair',
  learn: '#/pair/learn',
  setup: '#/setup',
};

export function go(to: string) {
  location.hash = to;
}
