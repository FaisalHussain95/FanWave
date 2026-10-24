// Browser storage for per-device conveniences only (last fan, setup done).
// Anything that matters lives on the hub.

export function read(key: string): string | null {
  try {
    return localStorage.getItem(key);
  } catch {
    return null;
  }
}

export function write(key: string, value: string) {
  try {
    localStorage.setItem(key, value);
  } catch {
    /* private mode or blocked storage: nothing to remember */
  }
}
