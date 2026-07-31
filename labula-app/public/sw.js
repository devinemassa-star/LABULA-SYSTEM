const CACHE_NAME = 'labula-v1';
const urlsToCache = [
  '/',
  '/index.html',
];

const FIREBASE_URL = 'https://labula-5bb9c-default-rtdb.firebaseio.com/labula_secret_2024/LABULA.json?auth=57P1q4qN3rmehxkLwljEburFrEXbmGx1Q64GI9a7';

let previousStatus = null;

self.addEventListener('install', event => {
  event.waitUntil(
    caches.open(CACHE_NAME).then(cache => {
      return cache.addAll(urlsToCache).catch(() => {});
    })
  );
  self.skipWaiting();
});

self.addEventListener('activate', event => {
  event.waitUntil(
    caches.keys().then(cacheNames => {
      return Promise.all(
        cacheNames.map(cacheName => {
          if (cacheName !== CACHE_NAME) {
            return caches.delete(cacheName);
          }
        })
      );
    })
  );
  self.clients.claim();
});

self.addEventListener('fetch', event => {
  if (event.request.method !== 'GET') {
    return;
  }

  event.respondWith(
    fetch(event.request)
      .then(response => {
        if (event.request.url.includes('firebaseio.com')) {
          return response;
        }
        const responseToCache = response.clone();
        caches.open(CACHE_NAME).then(cache => {
          cache.put(event.request, responseToCache);
        });
        return response;
      })
      .catch(() => {
        return caches.match(event.request).then(response => {
          return response || new Response('Offline - content unavailable', {
            status: 503,
            statusText: 'Service Unavailable',
            headers: new Headers({ 'Content-Type': 'text/plain' })
          });
        });
      })
  );
});

self.addEventListener('push', event => {
  const data = event.data ? event.data.json() : {};
  const title = data.title || 'LABULA Alert';
  const body = data.body || 'Gas monitoring alert';
  const icon = '/labula.png';
  const badge = '/labula.png';

  event.waitUntil(
    self.registration.showNotification(title, {
      body,
      icon,
      badge,
      vibrate: [200, 100, 200],
      requireInteraction: true,
      tag: 'labula-alarm',
      renotify: true,
    })
  );
});

self.addEventListener('notificationclick', event => {
  event.notification.close();
  event.waitUntil(
    clients.matchAll({ type: 'window', includeUncontrolled: true }).then(clientList => {
      for (const client of clientList) {
        if (client.url.includes('/') && 'focus' in client) {
          return client.focus();
        }
      }
      if (clients.openWindow) {
        return clients.openWindow('/');
      }
    })
  );
});

self.addEventListener('periodicsync', event => {
  if (event.tag === 'labula-status-check') {
    event.waitUntil(checkFirebaseStatus());
  }
});

async function checkFirebaseStatus() {
  try {
    const response = await fetch(FIREBASE_URL);
    if (!response.ok) return;
    const data = await response.json();
    if (!data) return;

    const currentStatus = data.status || 'UNKNOWN';

    if (previousStatus && previousStatus !== currentStatus) {
      let title = '';
      let body = '';

      if (currentStatus === 'ALARM') {
        title = '🚨 LABULA Gas Alarm';
        body = `Gas level critical: ${data.gasLevel || 0}/4095. Fan and vent activated.`;
      } else if (currentStatus === 'MANUAL_OVERRIDE') {
        title = '🔧 LABULA Manual Override';
        body = 'Manual override is now active.';
      } else if (currentStatus === 'NORMAL' && previousStatus !== 'NORMAL') {
        title = '✅ LABULA Back to Normal';
        body = 'Gas levels are normal. All systems operational.';
      }

      if (title && body) {
        self.registration.showNotification(title, {
          body,
          icon: '/labula.png',
          badge: '/labula.png',
          vibrate: [200, 100, 200],
          requireInteraction: true,
          tag: 'labula-alarm',
          renotify: true,
        });
      }
    }

    previousStatus = currentStatus;
  } catch (err) {
    // Silently fail - polling is best-effort
  }
}
