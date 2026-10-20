// How requests reach the device. The real transport uses fetch/EventSource; the
// mock device (npm run dev:mock, tests) installs its own.

export interface TransportResponse {
  status: number;
  body: unknown;
}

export type StreamHandler = (event: string, data: unknown) => void;

export interface Transport {
  request(method: string, path: string, body: unknown, token: string | null): Promise<TransportResponse>;
  // Subscribes to live updates; returns an unsubscribe function.
  stream(token: string, onEvent: StreamHandler, onError: () => void): () => void;
}

export const httpTransport: Transport = {
  async request(method, path, body, token) {
    const headers: Record<string, string> = {};
    if (token) headers.Authorization = `Bearer ${token}`;
    if (body !== undefined) headers['Content-Type'] = 'application/json';
    const res = await fetch(path, {
      method,
      headers,
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    const text = await res.text();
    let parsed: unknown;
    try {
      parsed = text ? JSON.parse(text) : null;
    } catch {
      parsed = { error: 'bad_response', message: text.slice(0, 200) };
    }
    return { status: res.status, body: parsed };
  },

  stream(token, onEvent, onError) {
    const source = new EventSource(`/api/stream?token=${encodeURIComponent(token)}`);
    const forward = (name: string) => (e: MessageEvent) => {
      try {
        onEvent(name, JSON.parse(e.data));
      } catch {
        /* ignore malformed frames */
      }
    };
    source.addEventListener('status', forward('status'));
    source.addEventListener('event', forward('event'));
    source.onerror = onError;
    return () => source.close();
  },
};

let current: Transport = httpTransport;

export function setTransport(t: Transport) {
  current = t;
}

export function transport(): Transport {
  return current;
}
